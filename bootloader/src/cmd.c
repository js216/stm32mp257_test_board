// SPDX-License-Identifier: BSD-3-Clause

/**
 * @file cmd.c
 * @brief Command line interface.
 * @copyright 2025-2026 Jakob Kastelic
 *
 * Ported from the stm32mp135_test_board bootloader. The line editor (history,
 * tab completion, escape handling) is preserved verbatim; only the command
 * table is trimmed to the commands implemented so far. The hardware commands
 * (reset, ddr, sd, jump, diag, ...) are re-enabled as their drivers land in
 * later steps -- see the commented block in cmd_list[].
 */

#include "cmd.h"
#include "console.h"
#include "printf.h"
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define CMD_MAX_LEN  32
#define HISTORY_SIZE 8

struct cmd_defaults {
   const char *label;   /* logical item name, e.g. "linux", "dtb" */
   uint32_t len_blocks; /* length in SD-card blocks */
   uint32_t sd_block;   /* starting SD-card block */
   uint32_t dest_addr;  /* destination memory address */
};

struct cmd {
   const char *name;
   const char *syntax;
   const char *summary;
   const struct cmd_defaults *defaults;
   size_t num_defaults;
   void (*handler)(int argc, uint32_t, uint32_t, uint32_t);
};

static const struct cmd cmd_list[] = {
    {
     .name         = "help",
     .syntax       = "",
     .summary      = "Display this help message",
     .defaults     = NULL,
     .num_defaults = 0,
     .handler      = cmd_help,
     },
    /*
     * Re-enabled as their drivers are ported in later steps:
     *   reset, print_ddr, align_test (step 4: ddr)
     *   load_sd, two, mbr_load       (step 5: sd)
     *   jump, diag                   (step 7: boot)
     */
};

#define CMD_COUNT (sizeof(cmd_list) / sizeof(cmd_list[0]))

/* cmd line buffer */
static char line_buf[CMD_MAX_LEN];
static size_t line_len = 0;

/* command history ring buffer */
static char history[HISTORY_SIZE][CMD_MAX_LEN];
static int history_head  = 0;
static int history_count = 0;
static int history_index = -1;

static void cmd_prompt(void)
{
   line_len = 0;
   memset(line_buf, 0, CMD_MAX_LEN);
   my_printf("> ");
}

void cmd_init(void)
{
   my_printf("\r\nSTM32MP257 bootloader\r\n");
   cmd_prompt();
}

static void line_erase(void)
{
   my_printf("\x1B[2K\x1B[0G");
   cmd_prompt();
}

static void line_load(const char *src)
{
   line_erase();
   strncpy(line_buf, src, CMD_MAX_LEN);
   line_buf[CMD_MAX_LEN - 1] = '\0';
   line_len                  = strlen(line_buf);
   my_printf("%s", line_buf);
}

static void history_add(const char *line)
{
   line_buf[line_len] = '\0';

   if (line[0] == '\0') {
      return;
   }

   strncpy(history[history_head], line, CMD_MAX_LEN);
   history[history_head][CMD_MAX_LEN - 1] = '\0';

   history_head = (history_head + 1) % HISTORY_SIZE;

   if (history_count < HISTORY_SIZE) {
      history_count++;
   }

   history_index = -1;
}

static void history_prev(void)
{
   if (history_count == 0) {
      return;
   }

   if (history_index < 0) {
      history_index = (history_head - 1 + HISTORY_SIZE) % HISTORY_SIZE;
   } else {
      int oldest = (history_head - history_count + HISTORY_SIZE) % HISTORY_SIZE;
      if (history_index != oldest) {
         history_index = (history_index - 1 + HISTORY_SIZE) % HISTORY_SIZE;
      }
   }

   line_load(history[history_index]);
}

static void history_next(void)
{
   if (history_index < 0) {
      return;
   }

   history_index = (history_index + 1) % HISTORY_SIZE;

   if (history_index == history_head) {
      history_index = -1;
      line_load("");
   } else {
      line_load(history[history_index]);
   }
}

static inline int my_isspace(int c)
{
   return c == ' ' || c == '\f' || c == '\n' || c == '\r' || c == '\t' ||
          c == '\v';
}

static int parse_args_uint32(uint32_t *arg1, uint32_t *arg2, uint32_t *arg3)
{
   uint32_t *args[3] = {arg1, arg2, arg3};
   char *p           = line_buf;
   char *end         = NULL;
   int count         = 0;

   /* skip command */
   while (*p && !my_isspace((unsigned char)*p))
      p++;
   while (*p && my_isspace((unsigned char)*p))
      p++;

   for (int i = 0; i < 3; i++) {
      if (!*p)
         break;

      /* Accept a "name=" prefix and parse only the integer after it. */
      char *q = p;
      while ((*q >= 'A' && *q <= 'Z') || (*q >= 'a' && *q <= 'z') ||
             *q == '_')
         q++;
      if (q != p && *q == '=')
         p = q + 1;

      *args[i] = strtoul(p, &end, 0);
      if (end == p)
         break;

      count++;
      p = end;

      while (*p && my_isspace((unsigned char)*p))
         p++;
   }

   return count;
}

static void execute_command(void)
{
   if (line_len == 0) {
      cmd_prompt();
      return;
   }

   line_buf[line_len] = '\0';

   char *space    = strchr(line_buf, ' ');
   size_t cmd_len = space ? (size_t)(space - line_buf) : line_len;

   size_t found            = 0;
   const struct cmd *match = NULL;

   for (size_t i = 0; i < CMD_COUNT; i++) {
      if (strncmp(line_buf, cmd_list[i].name, cmd_len) == 0) {
         found++;
         match = &cmd_list[i];
      }
   }

   if (found == 0) {
      my_printf("Unknown command '%s'.\r\n", line_buf);
      cmd_help(0, 0, 0, 0);
      cmd_prompt();
      return;
   }

   if (found > 1) {
      my_printf("Ambiguous command '%.*s'.\r\n", (int)cmd_len, line_buf);
      cmd_prompt();
      return;
   }

   uint32_t arg1 = 0;
   uint32_t arg2 = 0;
   uint32_t arg3 = 0;
   int argc      = parse_args_uint32(&arg1, &arg2, &arg3);

   match->handler(argc, arg1, arg2, arg3);
   cmd_prompt();
}

static void cmd_tab_completion(void)
{
   size_t matches    = 0;
   const char *match = NULL;

   for (size_t i = 0; i < CMD_COUNT; i++) {
      if (strncmp(line_buf, cmd_list[i].name, line_len) == 0) {
         matches++;
         match = cmd_list[i].name;
      }
   }

   if (matches == 1) {
      size_t len = strlen(match);
      while (line_len < len) {
         char c               = match[line_len];
         line_buf[line_len++] = c;
         my_printf("%c", c);
      }
   } else if (matches > 1) {
      my_printf("\r\n");
      for (size_t i = 0; i < CMD_COUNT; i++) {
         if (strncmp(line_buf, cmd_list[i].name, line_len) == 0)
            my_printf("%s  ", cmd_list[i].name);
      }
      my_printf("\r\n> %s", line_buf);
   }
}

static int try_handle_escape(char byte)
{
   static int esc_state = 0;

   if (esc_state == 1) {
      if (byte == '[') {
         esc_state = 2;
         return 1;
      }
      esc_state = 0;
      return 1;
   }

   if (esc_state == 2) {
      if (byte == 'A') /* Up arrow */
         history_prev();
      else if (byte == 'B') /* Down arrow */
         history_next();
      esc_state = 0;
      return 1;
   }

   if (byte == 0x1B) { /* ESC */
      esc_state = 1;
      return 1;
   }

   return 0;
}

void cmd_poll(void)
{
   while (!console_rx_empty()) {
      char byte = console_rx_get();

      if (try_handle_escape(byte))
         continue;

      if (byte == '\r' || byte == '\n') {
         my_printf("\r\n");
         history_add(line_buf);
         execute_command();
      }

      else if ((byte == '\b' || byte == 0x7F)) {
         if (line_len > 0) {
            line_len--;
            line_buf[line_len] = '\0';
            my_printf("\b \b");
         }
      }

      else if (byte >= 0x20 && byte <= 0x7E) {
         if (line_len < CMD_MAX_LEN - 1) {
            line_buf[line_len++] = byte;
            my_printf("%c", byte);
         }
      }

      else if (byte == '\t') {
         cmd_tab_completion();
      }

      else if (byte == 0x0C) { /* Ctrl-L */
         my_printf("%c", byte);
         cmd_prompt();
      }

      else {
         my_printf("^%c", (uint8_t)byte ^ 0x40U);
      }
   }

   if (console_interrupted()) {
      cmd_prompt();
   }
}

void cmd_help(int argc, uint32_t arg1, uint32_t arg2, uint32_t arg3)
{
   (void)argc;
   (void)arg1;
   (void)arg2;
   (void)arg3;

   my_printf("Available commands:\r\n\r\n");

   for (size_t i = 0; i < CMD_COUNT; i++) {
      const struct cmd *c = &cmd_list[i];

      my_printf("  %s %s\r\n", c->name, c->syntax);
      my_printf("    %s\r\n", c->summary);
      my_printf("\r\n");
   }
}
