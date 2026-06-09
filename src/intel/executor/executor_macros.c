/*
 * Copyright © 2024 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include <ctype.h>
#include <stdlib.h>

#include "util/ralloc.h"

#include "executor.h"

static bool
is_comment(slice s)
{
   return s.len >= 2 && s.data[0] == '/' && s.data[1] == '/';
}

slice
strip_spaces(slice s)
{
   while (!slice_is_empty(s) && isspace((unsigned char)s.data[0]))
      s = slice_substr_from(s, 1);

   while (!slice_is_empty(s) && isspace((unsigned char)s.data[s.len - 1]))
      s = slice_substr_to(s, s.len - 1);

   return s;
}

slice
trim_comments(slice s)
{
   for (int i = 0; i < s.len - 1; i++) {
      if (s.data[i] == '/' && s.data[i + 1] == '/')
         return slice_substr_to(s, i);
   }
   return s;
}

static bool
slice_parse_grf(slice reg, unsigned *nr)
{
   if (reg.len < 2 || reg.data[0] != 'r' ||
       !isdigit((unsigned char)reg.data[1]))
      return false;

   unsigned value = 0;
   for (int i = 1; i < reg.len && isdigit((unsigned char)reg.data[i]); i++)
      value = value * 10 + reg.data[i] - '0';

   *nr = value;
   return true;
}


typedef struct {
   slice *args;
   int    count;
} parse_args_result;

static parse_args_result
parse_args(void *mem_ctx, slice args)
{
   parse_args_result r = {0};
   args = trim_comments(args);

   while (true) {
      args = strip_spaces(args);

      if (slice_is_empty(args))
         break;

      slice_cut_result cut = slice_cut_any(args, " \t");
      r.args = reralloc_array_size(mem_ctx, r.args, sizeof(slice), r.count + 1);
      r.args[r.count++] = cut.before;
      args = cut.after;
   }

   return r;
}

static void
executor_macro_mov(executor_context *ec, char **src, slice line)
{
   slice args = slice_strip_prefix(line, slice_from_cstr("@mov"));
   parse_args_result r = parse_args(ec->mem_ctx, args);

   if (r.count != 2)
      failf("@mov needs 2 arguments, found %d\n", r.count);

   slice reg = r.args[0];
   char *value = slice_to_cstr(ec->mem_ctx, r.args[1]);
   const unsigned width = ec->devinfo->ver >= 20 ? 16 : 8;

   if (strchr(value, '.')) {
      union {
         float f;
         uint32_t u;
      } val;

      val.f = strtof(value, NULL);
      ralloc_asprintf_append(src,
         "mov (%u) %.*s:f 0x%08x:f\n",
         width, SLICE_FMT(reg), val.u);
   } else {
      for (char *x = value; *x; x++)
         *x = tolower(*x);

      ralloc_asprintf_append(src,
         "mov (%u) %.*s %s\n",
         width, SLICE_FMT(reg), value);
   }
}

static void
executor_emit_syncnop(executor_context *ec, char **src)
{
   switch (ec->devinfo->verx10) {
   case 90:
   case 110: {
      /* Not needed. */
      break;
   }

   case 120: {
      ralloc_strcat(src,
         "(W) sync.nop (8) null {@1,$1.dst}\n");
      break;
   }

   case 125:
   case 200:
   case 300:
   case 350: {
      ralloc_strcat(src,
         "(W) sync.nop (8) null {A@1,$1.dst}\n");
      break;
   }

   default:
      UNREACHABLE("invalid gfx version");
   }
}

static void
executor_macro_syncnop(executor_context *ec, char **src, slice line)
{
   executor_emit_syncnop(ec, src);
}

static void
executor_macro_eot(executor_context *ec, char **src, slice line)
{
   switch (ec->devinfo->verx10) {
   case 90:
   case 110: {
      ralloc_strcat(src,
         "(W) mov (8) r127 r0\n"
         "(W) sends.ts (1) null r127:1 null:0 0x00000000 0x82000010 {EOT}\n");
      break;
   }

   case 120: {
      ralloc_strcat(src,
         "(W) mov (8) r127 r0\n"
         "(W) send.ts (1) null r127:1 null:0 0x00000000 0x02000000 {@1,EOT}\n");
      break;
   }

   case 125:
   case 200:
   case 300:
   case 350: {
      const unsigned slots = ec->devinfo->verx10 >= 200 ? 16 : 8;
      ralloc_asprintf_append(src,
         "(W) mov (%u) r127 r0\n"
         "(W) send.gtwy (1) null r127:1 null:0 0x00000000 0x02000000 {A@1,EOT}\n",
         slots);
      break;
   }

   default:
      UNREACHABLE("invalid gfx version");
   }
}

static void
executor_macro_id(executor_context *ec, char **src, slice line)
{
   slice args = slice_strip_prefix(line, slice_from_cstr("@id"));
   parse_args_result r = parse_args(ec->mem_ctx, args);

   if (r.count != 1)
      failf("@id needs 1 argument, found %d", r.count);

   slice reg = r.args[0];


   switch (ec->devinfo->verx10) {
   case 90:
   case 110:
   case 120: {
      ralloc_asprintf_append(src,
         "(W) mov (8) r127:uw 0x76543210:v\n"
         "(W) mov (8) %.*s r127:uw {@1}\n",
         SLICE_FMT(reg));
      break;
   }

   case 125: {
      ralloc_asprintf_append(src,
         "(W) mov (8) r127:uw 0x76543210:v\n"
         "(W) mov (8) %.*s r127:uw {A@1}\n",
         SLICE_FMT(reg));
      break;
   }

   case 200:
   case 300:
   case 350: {
      ralloc_asprintf_append(src,
         "(W) mov (8) r127:uw 0x76543210:v\n"
         "(W) add (8) r127.8:uw r127:uw 8:uw {A@1}\n"
         "(W) mov (16) %.*s r127:uw {A@1}\n",
         SLICE_FMT(reg));
      break;
   }

   default:
      UNREACHABLE("invalid gfx version");
   }
}

static void
executor_macro_write(executor_context *ec, char **src, slice line)
{
   slice args = slice_strip_prefix(line, slice_from_cstr("@write"));
   parse_args_result r = parse_args(ec->mem_ctx, args);

   if (r.count != 2)
      failf("@write needs 2 arguments, found %d\n", r.count);

   slice offset_reg = r.args[0];
   slice data_reg   = r.args[1];

   assert(ec->bo.data.addr <= 0xFFFFFFFF);
   uint32_t base_addr = ec->bo.data.addr;

   switch (ec->devinfo->verx10) {
   case 90:
   case 110:
   case 120: {
      const char *send_op = ec->devinfo->verx10 < 120 ? "sends.hdc1" : "send.hdc1";
      ralloc_asprintf_append(src,
         "mul (8) r127 %.*s 0x4:uw {@1}\n"
         "add (8) r127 r127 0x%08x {@1}\n"
         "%s (8) null r127:1 %.*s:1 0x00000040 0x02026efd {@1,$1}\n",
         SLICE_FMT(offset_reg), base_addr, send_op, SLICE_FMT(data_reg));
      executor_emit_syncnop(ec, src);
      break;
   }

   case 125: {
      ralloc_asprintf_append(src,
         "mul (8) r127 %.*s 0x4:uw {A@1}\n"
         "add (8) r127 r127 0x%08x {A@1}\n"
         "store.ugm.d32.a32 (8) r127:1 %.*s:1 {A@1,$1}\n",
         SLICE_FMT(offset_reg), base_addr, SLICE_FMT(data_reg));
      executor_emit_syncnop(ec, src);
      break;
   }

   case 200:
   case 300:
   case 350: {
      ralloc_asprintf_append(src,
         "mul (16) r127 %.*s 0x4:uw {A@1}\n"
         "add (16) r127 r127 0x%08x {A@1}\n"
         "store.ugm.d32.a32 (16) r127:1 %.*s:1 {A@1,$1}\n",
         SLICE_FMT(offset_reg), base_addr, SLICE_FMT(data_reg));
      executor_emit_syncnop(ec, src);
      break;
   }

   default:
      UNREACHABLE("invalid gfx version");
   }
}

static void
executor_macro_read(executor_context *ec, char **src, slice line)
{
   slice args = slice_strip_prefix(line, slice_from_cstr("@read"));
   parse_args_result r = parse_args(ec->mem_ctx, args);

   if (r.count != 2)
      failf("@read needs 2 arguments, found %d\n", r.count);

   /* Order follows underlying SEND, destination first. */
   slice data_reg   = r.args[0];
   slice offset_reg = r.args[1];

   assert(ec->bo.data.addr <= 0xFFFFFFFF);
   uint32_t base_addr = ec->bo.data.addr;

   switch (ec->devinfo->verx10) {
   case 90:
   case 110:
   case 120: {
      const char *send_op = ec->devinfo->verx10 < 120 ? "sends.hdc1" : "send.hdc1";
      ralloc_asprintf_append(src,
         "mul (8) r127 %.*s 0x4:uw {@1}\n"
         "add (8) r127 r127 0x%08x {@1}\n"
         "%s (8) %.*s r127:1 null:0 0x00000000 0x02106efd {@1,$1}\n",
         SLICE_FMT(offset_reg), base_addr, send_op, SLICE_FMT(data_reg));
      executor_emit_syncnop(ec, src);
      break;
   }

   case 125: {
      ralloc_asprintf_append(src,
         "mul (8) r127 %.*s 0x4:uw {A@1}\n"
         "add (8) r127 r127 0x%08x {A@1}\n"
         "load.ugm.d32.a32 (8) %.*s:1 r127:1 {A@1,$1}\n",
         SLICE_FMT(offset_reg), base_addr, SLICE_FMT(data_reg));
      executor_emit_syncnop(ec, src);
      break;
   }

   case 200:
   case 300:
   case 350: {
      ralloc_asprintf_append(src,
         "mul (16) r127 %.*s 0x4:uw {A@1}\n"
         "add (16) r127 r127 0x%08x {A@1}\n"
         "load.ugm.d32.a32 (16) %.*s:1 r127:1 {A@1,$1}\n",
         SLICE_FMT(offset_reg), base_addr, SLICE_FMT(data_reg));
      executor_emit_syncnop(ec, src);
      break;
   }

   default:
      UNREACHABLE("invalid gfx version");
   }
}

static slice
find_macro_symbol(slice line)
{
   line = strip_spaces(line);
   return !slice_is_empty(line) && line.data[0] == '@' ? line : (slice){};
}

static bool
match_macro_name(const char *name, slice line)
{
   slice name_slice = slice_from_cstr(name);
   if (!slice_starts_with(line, name_slice))
      return false;

   line = slice_substr_from(line, name_slice.len);
   return slice_is_empty(line) || isspace(*line.data) || is_comment(line);
}

const char *
executor_apply_macros(executor_context *ec, slice original_src)
{
   slice remaining = original_src;

   /* Create a ralloc'ed empty string so can call append to it later. */
   char *src = ralloc_strdup(ec->mem_ctx, "");

   /* TODO: Create a @send macro for common combinations of MsgDesc. */
   static const struct {
      const char *name;
      void (*func)(executor_context *ec, char **output, slice line);
   } macros[] = {
      { "@eot",      executor_macro_eot },
      { "@mov",      executor_macro_mov },
      { "@write",    executor_macro_write },
      { "@read",     executor_macro_read },
      { "@id",       executor_macro_id },
      { "@syncnop",  executor_macro_syncnop },
   };

   while (!slice_is_empty(remaining)) {
      slice_cut_result cut = slice_cut_any(remaining, "\n\r");
      slice line = cut.before;
      remaining = cut.after;

      slice macro = find_macro_symbol(line);
      if (slice_is_empty(macro)) {
         ralloc_asprintf_append(&src, "%.*s\n", SLICE_FMT(line));
      } else {
         bool found = false;
         for (int i = 0; i < ARRAY_SIZE(macros); i++) {
            if (match_macro_name(macros[i].name, macro)) {
               macros[i].func(ec, &src, macro);
               found = true;
               break;
            }
         }
         if (!found)
            failf("unsupported macro line: %.*s", SLICE_FMT(macro));
      }
   }

   return src;
}
