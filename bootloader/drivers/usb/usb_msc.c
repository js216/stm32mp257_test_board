// SPDX-License-Identifier: BSD-3-Clause

/**
 * @file usb_msc.c
 * @brief USB Mass Storage (bulk-only transport) device exposing the SD card.
 * @copyright 2026 Jakob Kastelic
 *
 * A mass-storage class for the carried TF-A usb_device framework + DWC3
 * driver (which TF-A only ever used for DFU on EP0). Implements the BOT
 * protocol with the SCSI transparent command set (the subset every OS
 * needs: INQUIRY, TEST UNIT READY, READ CAPACITY(10), READ(10), WRITE(10),
 * REQUEST SENSE, MODE SENSE(6), START STOP, MEDIUM REMOVAL), one LUN
 * backed by drivers/sd.h. Modelled on the ST USBD MSC middleware that the
 * stm32mp135 bootloader uses, rewritten for this framework.
 */

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <drivers/st/usb_dwc3.h>
#include <drivers/usb_device.h>

#include "lib/mmio.h"
#include "printf.h"
#include "sd.h"
#include "usb_msc.h"

#define MSC_EP_IN   0x81U
#define MSC_EP_OUT  0x01U
#define MSC_EP_NUM  1U
#define MSC_MPS_HS  512U

/* Bench contract (test_serv msc.mp257 instance): VID 0x0483 with a PID
 * distinct from the MP135 baremetal MSC (0x571d) so the bench can tell the
 * board families apart even when both are in their bootloader MSC stage;
 * the chip-UID iSerial then distinguishes individual boards. Keep in sync
 * with the bench config (bug report filed 2026-06-11). */
#define USBD_VID 0x0483U
#define USBD_PID 0x5720U /* MP257 baremetal MSC bootloader */

#define CBW_SIGNATURE 0x43425355U
#define CSW_SIGNATURE 0x53425355U
#define CBW_LENGTH    31U
#define CSW_LENGTH    13U

/* SCSI opcodes */
#define SCSI_TEST_UNIT_READY  0x00U
#define SCSI_REQUEST_SENSE    0x03U
#define SCSI_INQUIRY          0x12U
#define SCSI_MODE_SENSE6      0x1AU
#define SCSI_START_STOP       0x1BU
#define SCSI_MEDIUM_REMOVAL   0x1EU
#define SCSI_READ_FORMAT_CAP  0x23U
#define SCSI_READ_CAPACITY10  0x25U
#define SCSI_READ10           0x28U
#define SCSI_WRITE10          0x2AU

#define BLOCK_SIZE 512U
/* One bulk transaction moves up to this much (multiple of the packet size,
 * so the DWC3 driver never needs its bounce buffer for data). */
#define CHUNK_BYTES (16U * 1024U)

enum bot_state {
   BOT_IDLE,     /* waiting for a CBW */
   BOT_DATA_IN,  /* sending READ10 data */
   BOT_DATA_OUT, /* receiving WRITE10 data */
   BOT_CSW,      /* CSW queued on the IN endpoint */
};

struct __attribute__((packed)) bot_cbw {
   uint32_t signature;
   uint32_t tag;
   uint32_t data_length;
   uint8_t flags;
   uint8_t lun;
   uint8_t cb_length;
   uint8_t cb[16];
};

struct __attribute__((packed)) bot_csw {
   uint32_t signature;
   uint32_t tag;
   uint32_t data_residue;
   uint8_t status; /* 0 pass, 1 fail */
};

static struct usb_handle *msc_pdev;
static dwc3_handle_t *msc_dwc3;

static enum bot_state bot_state;
static struct bot_cbw cbw __attribute__((aligned(64)));
static struct bot_csw csw __attribute__((aligned(64)));
static uint8_t xfer_buf[CHUNK_BYTES] __attribute__((aligned(512)));

/* Current READ10/WRITE10 progress (in blocks). */
static uint32_t io_lba;
static uint32_t io_blocks_left;
static uint32_t io_chunk_blocks;

/* Sense data for the last failed command. */
static uint8_t sense_key;
static uint8_t sense_asc;

unsigned long msc_read_count;
unsigned long msc_write_count;
unsigned long msc_setup_count;
unsigned long msc_init_count;

static void msc_arm_cbw(void)
{
   bot_state = BOT_IDLE;
   (void)usb_core_receive(msc_pdev, MSC_EP_OUT, (uint8_t *)&cbw, CBW_LENGTH);
}

static void msc_send_csw(uint8_t status)
{
   csw.signature = CSW_SIGNATURE;
   csw.tag = cbw.tag;
   csw.data_residue = 0U;
   csw.status = status;
   bot_state = BOT_CSW;
   (void)usb_core_transmit(msc_pdev, MSC_EP_IN, (uint8_t *)&csw, CSW_LENGTH);
}

static void msc_fail(uint8_t key, uint8_t asc)
{
   sense_key = key;
   sense_asc = asc;
   msc_send_csw(1U);
}

/* Send a data-in reply capped to the host-requested length, then a CSW. */
static void msc_send_data(const uint8_t *data, uint32_t len)
{
   if (len > cbw.data_length) {
      len = cbw.data_length;
   }
   memcpy(xfer_buf, data, len);
   bot_state = BOT_DATA_IN;
   io_blocks_left = 0U;
   (void)usb_core_transmit(msc_pdev, MSC_EP_IN, xfer_buf, len);
}

static void msc_continue_read(void)
{
   uint32_t blocks = io_blocks_left;

   if (blocks > (CHUNK_BYTES / BLOCK_SIZE)) {
      blocks = CHUNK_BYTES / BLOCK_SIZE;
   }

   if (sd_read(io_lba, (uintptr_t)xfer_buf, blocks * BLOCK_SIZE) != 0) {
      msc_fail(0x04U, 0x11U); /* hardware error, unrecovered read */
      return;
   }

   io_lba += blocks;
   io_blocks_left -= blocks;
   io_chunk_blocks = blocks;
   msc_read_count += blocks;
   bot_state = BOT_DATA_IN;
   (void)usb_core_transmit(msc_pdev, MSC_EP_IN, xfer_buf,
                           blocks * BLOCK_SIZE);
}

static void msc_arm_write_chunk(void)
{
   uint32_t blocks = io_blocks_left;

   if (blocks > (CHUNK_BYTES / BLOCK_SIZE)) {
      blocks = CHUNK_BYTES / BLOCK_SIZE;
   }

   io_chunk_blocks = blocks;
   bot_state = BOT_DATA_OUT;
   (void)usb_core_receive(msc_pdev, MSC_EP_OUT, xfer_buf,
                          blocks * BLOCK_SIZE);
}

static void msc_cmd_dispatch(void)
{
   const uint8_t *cb = cbw.cb;

   switch (cb[0]) {
   case SCSI_TEST_UNIT_READY:
   case SCSI_START_STOP:
   case SCSI_MEDIUM_REMOVAL:
      msc_send_csw(0U);
      break;

   case SCSI_INQUIRY: {
      static const uint8_t inq[36] = {
         0x00, 0x80, 0x02, 0x02, 31, 0, 0, 0,
         'S', 'T', 'M', '3', '2', 'M', 'P', '2',
         'F', 'S', 'B', 'L', ' ', 'S', 'D', ' ',
         'c', 'a', 'r', 'd', ' ', ' ', ' ', ' ',
         '1', '.', '0', ' '
      };
      msc_send_data(inq, sizeof(inq));
      break;
   }

   case SCSI_READ_CAPACITY10: {
      uint32_t last = (uint32_t)(sd_size_bytes() / BLOCK_SIZE) - 1U;
      uint8_t cap[8] = {
         (uint8_t)(last >> 24), (uint8_t)(last >> 16),
         (uint8_t)(last >> 8),  (uint8_t)last,
         0, 0, (uint8_t)(BLOCK_SIZE >> 8), 0
      };
      msc_send_data(cap, sizeof(cap));
      break;
   }

   case SCSI_READ_FORMAT_CAP: {
      uint32_t nblk = (uint32_t)(sd_size_bytes() / BLOCK_SIZE);
      uint8_t fmt[12] = {
         0, 0, 0, 8,
         (uint8_t)(nblk >> 24), (uint8_t)(nblk >> 16),
         (uint8_t)(nblk >> 8),  (uint8_t)nblk,
         0x02, /* formatted media */
         0, (uint8_t)(BLOCK_SIZE >> 8), 0
      };
      msc_send_data(fmt, sizeof(fmt));
      break;
   }

   case SCSI_MODE_SENSE6: {
      static const uint8_t sense6[4] = {3, 0, 0, 0};
      msc_send_data(sense6, sizeof(sense6));
      break;
   }

   case SCSI_REQUEST_SENSE: {
      uint8_t sns[18];
      memset(sns, 0, sizeof(sns));
      sns[0] = 0x70;
      sns[2] = sense_key;
      sns[7] = 10;
      sns[12] = sense_asc;
      sense_key = 0U;
      sense_asc = 0U;
      msc_send_data(sns, sizeof(sns));
      break;
   }

   case SCSI_READ10:
   case SCSI_WRITE10: {
      uint32_t lba = ((uint32_t)cb[2] << 24) | ((uint32_t)cb[3] << 16) |
                     ((uint32_t)cb[4] << 8) | cb[5];
      uint32_t cnt = ((uint32_t)cb[7] << 8) | cb[8];

      if (cnt == 0U) {
         msc_send_csw(0U);
         break;
      }
      if (((uint64_t)lba + cnt) > (sd_size_bytes() / BLOCK_SIZE)) {
         msc_fail(0x05U, 0x21U); /* illegal request, LBA out of range */
         break;
      }

      io_lba = lba;
      io_blocks_left = cnt;
      if (cb[0] == SCSI_READ10) {
         msc_continue_read();
      } else {
         msc_arm_write_chunk();
      }
      break;
   }

   default:
      msc_fail(0x05U, 0x20U); /* illegal request, invalid opcode */
      break;
   }
}

/* ---- usb_class callbacks ---- */

static uint8_t msc_init(struct usb_handle *pdev, uint8_t cfgidx)
{
   (void)cfgidx;
   msc_pdev = pdev;
   msc_init_count++;

   (void)usb_dwc3_ep_open(msc_dwc3, MSC_EP_OUT, USBD_EP_TYPE_BULK,
                          MSC_MPS_HS);
   (void)usb_dwc3_ep_open(msc_dwc3, MSC_EP_IN, USBD_EP_TYPE_BULK,
                          MSC_MPS_HS);
   msc_arm_cbw();
   return 0U;
}

static uint8_t msc_de_init(struct usb_handle *pdev, uint8_t cfgidx)
{
   (void)pdev;
   (void)cfgidx;
   return 0U;
}

static uint8_t msc_setup(struct usb_handle *pdev, struct usb_setup_req *req)
{
   static uint8_t max_lun;

   msc_setup_count++;
   if ((req->bm_request & 0x60U) == 0x20U) { /* class request */
      switch (req->b_request) {
      case 0xFEU: /* GET MAX LUN */
         max_lun = 0U;
         (void)usb_core_transmit_ep0(pdev, &max_lun, 1U);
         return 0U;
      case 0xFFU: /* BOT reset */
         msc_arm_cbw();
         return 0U;
      default:
         break;
      }
   }
   usb_core_ctl_error(pdev);
   return 1U;
}

static uint8_t msc_data_in(struct usb_handle *pdev, uint8_t epnum)
{
   (void)pdev;

   if (epnum != MSC_EP_NUM) {
      return 0U;
   }

   switch (bot_state) {
   case BOT_DATA_IN:
      if (io_blocks_left != 0U) {
         msc_continue_read();
      } else {
         msc_send_csw(0U);
      }
      break;
   case BOT_CSW:
      msc_arm_cbw();
      break;
   default:
      break;
   }
   return 0U;
}

static uint8_t msc_data_out(struct usb_handle *pdev, uint8_t epnum)
{
   if (epnum != MSC_EP_NUM) {
      return 0U;
   }

   switch (bot_state) {
   case BOT_IDLE:
      if ((pdev->data->out_ep[MSC_EP_NUM].xfer_count == CBW_LENGTH) &&
          (cbw.signature == CBW_SIGNATURE)) {
         msc_cmd_dispatch();
      } else {
         msc_fail(0x05U, 0x24U); /* invalid CBW */
      }
      break;

   case BOT_DATA_OUT:
      if (sd_write(io_lba, (uintptr_t)xfer_buf,
                   io_chunk_blocks * BLOCK_SIZE) != 0) {
         msc_fail(0x04U, 0x03U); /* hardware error, write fault */
         break;
      }
      io_lba += io_chunk_blocks;
      io_blocks_left -= io_chunk_blocks;
      msc_write_count += io_chunk_blocks;
      if (io_blocks_left != 0U) {
         msc_arm_write_chunk();
      } else {
         msc_send_csw(0U);
      }
      break;

   default:
      break;
   }
   return 0U;
}

static struct usb_class msc_class = {
   .init = msc_init,
   .de_init = msc_de_init,
   .setup = msc_setup,
   .data_in = msc_data_in,
   .data_out = msc_data_out,
};

/* ---- descriptors ---- */

static const uint8_t msc_device_desc[USB_LEN_DEV_DESC] = {
   USB_LEN_DEV_DESC, USB_DESC_TYPE_DEVICE,
   0x00, 0x02,             /* bcdUSB 2.00 */
   0x00, 0x00, 0x00,       /* class/sub/protocol: per interface */
   USB_MAX_EP0_SIZE,
   LOBYTE(USBD_VID), HIBYTE(USBD_VID),
   LOBYTE(USBD_PID), HIBYTE(USBD_PID),
   0x00, 0x02,             /* bcdDevice 2.00 */
   USBD_IDX_MFC_STR, USBD_IDX_PRODUCT_STR, USBD_IDX_SERIAL_STR,
   USBD_MAX_NUM_CONFIGURATION
};

#define MSC_CONFIG_DESC_SIZ 32U

static const uint8_t msc_config_desc[MSC_CONFIG_DESC_SIZ] = {
   /* configuration */
   0x09, USB_DESC_TYPE_CONFIGURATION,
   MSC_CONFIG_DESC_SIZ, 0x00,
   0x01,       /* one interface */
   0x01,       /* configuration value */
   0x00,       /* no configuration string */
   0xC0, 0x32, /* self powered, 100 mA */
   /* interface: MSC, SCSI transparent, BOT */
   0x09, USB_DESC_TYPE_INTERFACE,
   0x00, 0x00, 0x02, 0x08, 0x06, 0x50, 0x00,
   /* EP 0x81 bulk IN, 512 */
   0x07, USB_DESC_TYPE_ENDPOINT, MSC_EP_IN, 0x02,
   LOBYTE(MSC_MPS_HS), HIBYTE(MSC_MPS_HS), 0x00,
   /* EP 0x01 bulk OUT, 512 */
   0x07, USB_DESC_TYPE_ENDPOINT, MSC_EP_OUT, 0x02,
   LOBYTE(MSC_MPS_HS), HIBYTE(MSC_MPS_HS), 0x00,
};

static const uint8_t msc_lang_id_desc[USB_LEN_LANGID_STR_DESC] = {
   USB_LEN_LANGID_STR_DESC, USB_DESC_TYPE_STRING, 0x09, 0x04,
};

static const uint8_t msc_qualifier_desc[USB_LEN_DEV_QUALIFIER_DESC] = {
   USB_LEN_DEV_QUALIFIER_DESC, USB_DESC_TYPE_DEVICE_QUALIFIER,
   0x00, 0x02, 0x00, 0x00, 0x00, 0x40, 0x01, 0x00,
};

/* Build a USB string descriptor (ASCII -> UTF-16LE) in a static buffer. */
static uint8_t str_desc_buf[64];

static uint8_t *msc_string_desc(const char *ascii, uint16_t *length)
{
   unsigned int n = 2U;

   for (const char *p = ascii; (*p != '\0') && (n < (sizeof(str_desc_buf) - 2U)); p++) {
      str_desc_buf[n++] = (uint8_t)*p;
      str_desc_buf[n++] = 0U;
   }
   str_desc_buf[0] = (uint8_t)n;
   str_desc_buf[1] = USB_DESC_TYPE_STRING;
   *length = (uint16_t)n;
   return str_desc_buf;
}

static uint8_t *msc_get_device_desc(uint16_t *length)
{
   *length = sizeof(msc_device_desc);
   return (uint8_t *)msc_device_desc;
}

static uint8_t *msc_get_lang_id_desc(uint16_t *length)
{
   *length = sizeof(msc_lang_id_desc);
   return (uint8_t *)msc_lang_id_desc;
}

static uint8_t *msc_get_mfc_desc(uint16_t *length)
{
   return msc_string_desc("STMicroelectronics", length);
}

static uint8_t *msc_get_product_desc(uint16_t *length)
{
   return msc_string_desc("MP257 FSBL SD card", length);
}

static uint8_t *msc_get_serial_desc(uint16_t *length)
{
   /* The chip UID, exactly as the ROM's DFU iSerial presents it. The bench
    * matches msc.mp257 on this string. Hardcoded for now -- the proper
    * source is BSEC OTP, but the ROM does not shadow a readable copy and a
    * BSEC driver is not worth it for a single string. */
   return msc_string_desc("002600224C42501400313951", length);
}

static uint8_t *msc_get_configuration_desc(uint16_t *length)
{
   return msc_string_desc("MSC Config", length);
}

static uint8_t *msc_get_interface_desc(uint16_t *length)
{
   return msc_string_desc("MSC Interface", length);
}

static uint8_t *msc_get_usr_desc(uint8_t index, uint16_t *length)
{
   (void)index;
   return msc_string_desc("MSC", length);
}

static uint8_t *msc_get_config_desc(uint16_t *length)
{
   *length = sizeof(msc_config_desc);
   return (uint8_t *)msc_config_desc;
}

static uint8_t *msc_get_qualifier_desc(uint16_t *length)
{
   *length = sizeof(msc_qualifier_desc);
   return (uint8_t *)msc_qualifier_desc;
}

static const struct usb_desc msc_desc = {
   .get_device_desc = msc_get_device_desc,
   .get_lang_id_desc = msc_get_lang_id_desc,
   .get_manufacturer_desc = msc_get_mfc_desc,
   .get_product_desc = msc_get_product_desc,
   .get_configuration_desc = msc_get_configuration_desc,
   .get_serial_desc = msc_get_serial_desc,
   .get_interface_desc = msc_get_interface_desc,
   .get_usr_desc = msc_get_usr_desc,
   .get_config_desc = msc_get_config_desc,
   .get_device_qualifier_desc = msc_get_qualifier_desc,
   .get_other_speed_config_desc = NULL,
};

/* ---- public interface ---- */

static struct usb_handle usb_core_handle;
static struct pcd_handle pcd_handle;
static dwc3_handle_t dwc3_handle;

int usb_msc_start(void)
{
   if (sd_size_bytes() == 0ULL) {
      if (sd_init() != 0) {
         return -1;
      }
   }

   msc_dwc3 = &dwc3_handle;

   /* The USB3DR DMA master still carries the boot ROM's RIF credentials,
    * and its event-buffer/TRB writes never land in our SYSRAM page. Re-tag
    * the master secure/privileged CID1 exactly as TF-A BL2 does
    * (stm32_rifsc_ip_configure of RIMU 4 / RIFSC id 66). */
   mmio_setbits_32(0x42080018UL, BIT(2));        /* RISC_SECCFGR2 */
   mmio_setbits_32(0x42080038UL, BIT(2));        /* RISC_PRIVCFGR2 */
   mmio_write_32(0x42080C20UL, 0x314U);          /* RIMC_ATTR4 */

   pcd_handle.in_ep[0].maxpacket = USB_MAX_EP0_SIZE;
   pcd_handle.out_ep[0].maxpacket = USB_MAX_EP0_SIZE;
   usb_dwc3_init_driver(&usb_core_handle, &pcd_handle, &dwc3_handle,
                        (void *)USB_DWC3_BASE_ADDR);

   register_platform(&usb_core_handle, &msc_desc);

   usb_core_handle.class = &msc_class;
   usb_core_handle.class_data = NULL;

   msc_pdev = &usb_core_handle;

   if (usb_core_start(&usb_core_handle) != USBD_OK) {
      return -1;
   }

   return 0;
}

void usb_msc_poll(void)
{
   (void)usb_core_handle_it(&usb_core_handle);
}
