COPYRIGHT=u"""
/* Copyright © 2015-2021 Intel Corporation
 * Copyright © 2021 Collabora, Ltd.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */
"""

import argparse
import os
import re
from collections import namedtuple
from enum import Enum, auto
import xml.etree.ElementTree as et

from mako.template import Template

# Mesa-local imports must be declared in meson variable
# '{file_without_suffix}_depend_files'.
from vk_entrypoints import EntrypointParam, get_entrypoints_from_xml
from vk_extensions import filter_api, get_all_required

# These have hand-typed implementations in vk_cmd_enqueue.c
MANUAL_COMMANDS = [
    # This script doesn't know how to copy arrays in structs in arrays
    'CmdPushDescriptorSet',

    # The size of the elements is specified in a stride param
    'CmdDrawMultiEXT',
    'CmdDrawMultiIndexedEXT',

    # The VkPipelineLayout object could be released before the command is
    # executed
    'CmdBindDescriptorSets',

    # Incomplete struct copies which lead to an use after free.
    'CmdBuildAccelerationStructuresKHR',

    # pData's size cannot be calculated from the xml
    'CmdPushDescriptorSetWithTemplate2',
    'CmdPushDescriptorSetWithTemplate',
    'CmdPushConstants2',
    'CmdPushDescriptorSet2',

    # VkDispatchGraphCountInfoAMDX::infos is an array of
    # VkDispatchGraphInfoAMDX, but the xml specifies that it is a
    # VkDeviceOrHostAddressConstAMDX.
    'CmdDispatchGraphAMDX',
]

NO_ENQUEUE_COMMANDS = [
    # These don't return void
    'CmdSetPerformanceMarkerINTEL',
    'CmdSetPerformanceStreamMarkerINTEL',
    'CmdSetPerformanceOverrideINTEL',

    'CmdBuildAccelerationStructuresIndirectKHR',
]

TEMPLATE_H = Template(COPYRIGHT + """\
/* This file generated from ${filename}, don't edit directly. */

#pragma once

#include "util/list.h"
#include "util/ralloc.h"

#define VK_PROTOTYPES
#include <vulkan/vulkan_core.h>
#ifdef VK_ENABLE_BETA_EXTENSIONS
#include <vulkan/vulkan_beta.h>
#endif

#include "vk_internal_exts.h"

#ifdef __cplusplus
extern "C" {
#endif

struct vk_device_dispatch_table;

struct vk_cmd_queue {
   linear_ctx *ctx;
   struct list_head cmds;
};

enum vk_cmd_type {
% for c in commands:
% if c.guard is not None:
#ifdef ${c.guard}
% endif
   ${to_enum_name(c.name)},
% if c.guard is not None:
#endif // ${c.guard}
% endif
% endfor
   VK_CMD_TYPE_COUNT,
};

extern const char *vk_cmd_queue_type_names[];
extern size_t vk_cmd_queue_type_sizes[];

% for c in commands:
% if len(c.params) <= 1:             # Avoid "error C2016: C requires that a struct or union have at least one member"
<% continue %>
% endif
% if c.guard is not None:
#ifdef ${c.guard}
% endif
struct ${to_struct_name(c.name)} {
% for p in c.params[1:]:
   ${to_field_decl(p.decl)};
% endfor
};
% if c.guard is not None:
#endif // ${c.guard}
% endif
% endfor

struct vk_cmd_queue_entry;

/* this ordering must match vk_cmd_queue_entry */
struct vk_cmd_queue_entry_base {
   struct list_head cmd_link;
   enum vk_cmd_type type;
   void (*driver_free_cb)(struct vk_cmd_queue *queue,
                          struct vk_cmd_queue_entry *cmd);
};

/* this ordering must match vk_cmd_queue_entry_base */
struct vk_cmd_queue_entry {
   struct list_head cmd_link;
   enum vk_cmd_type type;
   void (*driver_free_cb)(struct vk_cmd_queue *queue,
                          struct vk_cmd_queue_entry *cmd);
   union {
% for c in commands:
% if len(c.params) <= 1:
<% continue %>
% endif
% if c.guard is not None:
#ifdef ${c.guard}
% endif
      struct ${to_struct_name(c.name)} ${to_struct_field_name(c.name)};
% if c.guard is not None:
#endif // ${c.guard}
% endif
% endfor
   } u;
};

% for c in commands:
% if c.name in manual_commands or c.name in no_enqueue_commands:
<% continue %>
% endif
% if c.guard is not None:
#ifdef ${c.guard}
% endif
  VkResult vk_enqueue_${to_underscore(c.name)}(struct vk_cmd_queue *queue
% for p in c.params[1:]:
   , ${p.decl}
% endfor
  );
% if c.guard is not None:
#endif // ${c.guard}
% endif

% endfor

void vk_free_queue(struct vk_cmd_queue *queue);

static inline void
vk_cmd_queue_init(struct vk_cmd_queue *queue)
{
   linear_opts opts = {
      .min_buffer_size = 64 * 1024
   };
   queue->ctx = linear_context_with_opts(NULL, &opts);
   list_inithead(&queue->cmds);
}

static inline void
vk_cmd_queue_reset(struct vk_cmd_queue *queue)
{
   vk_free_queue(queue);
   vk_cmd_queue_init(queue);
}

static inline void
vk_cmd_queue_finish(struct vk_cmd_queue *queue)
{
   vk_free_queue(queue);
}

void vk_cmd_queue_execute(struct vk_cmd_queue *queue,
                          VkCommandBuffer commandBuffer,
                          const struct vk_device_dispatch_table *disp);

#ifdef __cplusplus
}
#endif
""")

TEMPLATE_C = Template(COPYRIGHT + """
/* This file generated from ${filename}, don't edit directly. */

#include "${header}"

#define VK_PROTOTYPES
#include <vulkan/vulkan_core.h>
#ifdef VK_ENABLE_BETA_EXTENSIONS
#include <vulkan/vulkan_beta.h>
#endif

#include "vk_cmd_enqueue_entrypoints.h"
#include "vk_command_buffer.h"
#include "vk_dispatch_table.h"
#include "vk_device.h"

const char *vk_cmd_queue_type_names[] = {
% for c in commands:
% if c.guard is not None:
#ifdef ${c.guard}
% endif
   "${to_enum_name(c.name)}",
% if c.guard is not None:
#endif // ${c.guard}
% endif
% endfor
};

size_t vk_cmd_queue_type_sizes[] = {
% for c in commands:
% if c.guard is not None:
#ifdef ${c.guard}
% endif
% if len(c.params) > 1:
   sizeof(struct ${to_struct_name(c.name)}) +
% endif
   sizeof(struct vk_cmd_queue_entry_base),
% if c.guard is not None:
#endif // ${c.guard}
% endif
% endfor
};

% for c in commands:
% if c.guard is not None:
#ifdef ${c.guard}
% endif
% if c.name not in manual_commands and c.name not in no_enqueue_commands:
VkResult vk_enqueue_${to_underscore(c.name)}(struct vk_cmd_queue *queue
% for p in c.params[1:]:
, ${p.decl}
% endfor
)
{
   struct vk_cmd_queue_entry *cmd = linear_alloc_child(queue->ctx, vk_cmd_queue_type_sizes[${to_enum_name(c.name)}]);
   if (!cmd) return VK_ERROR_OUT_OF_HOST_MEMORY;

   cmd->type = ${to_enum_name(c.name)};
   cmd->driver_free_cb = NULL;
${get_params_copy(c, types)}}
% endif
% if c.guard is not None:
#endif // ${c.guard}
% endif

% endfor

void
vk_free_queue(struct vk_cmd_queue *queue)
{
   struct vk_cmd_queue_entry *tmp, *cmd;
   LIST_FOR_EACH_ENTRY_SAFE(cmd, tmp, &queue->cmds, cmd_link) {
      if (cmd->driver_free_cb)
         cmd->driver_free_cb(queue, cmd);
   }
   linear_free_context(queue->ctx);
}

void
vk_cmd_queue_execute(struct vk_cmd_queue *queue,
                     VkCommandBuffer commandBuffer,
                     const struct vk_device_dispatch_table *disp)
{
   list_for_each_entry(struct vk_cmd_queue_entry, cmd, &queue->cmds, cmd_link) {
      switch (cmd->type) {
% for c in commands:
% if c.guard is not None:
#ifdef ${c.guard}
% endif
      case ${to_enum_name(c.name)}:
          disp->${c.name}(commandBuffer
% for p in c.params[1:]:
             , cmd->u.${to_struct_field_name(c.name)}.${to_field_name(p.name)}\\
% endfor
          );
          break;
% if c.guard is not None:
#endif // ${c.guard}
% endif
% endfor
      default: UNREACHABLE("Unsupported command");
      }
   }
}

% for c in commands:
% if c.name in no_enqueue_commands:
/* TODO: Generate vk_cmd_enqueue_${c.name}() */
<% continue %>
% endif

% if c.guard is not None:
#ifdef ${c.guard}
% endif
<% assert c.return_type == 'void' %>

% if c.name in manual_commands:
/* vk_cmd_enqueue_${c.name}() is hand-typed in vk_cmd_enqueue.c */
% else:
VKAPI_ATTR void VKAPI_CALL
vk_cmd_enqueue_${c.name}(${c.decl_params()})
{
   VK_FROM_HANDLE(vk_command_buffer, cmd_buffer, commandBuffer);

   if (vk_command_buffer_has_error(cmd_buffer))
      return;
% if len(c.params) == 1:
   VkResult result = vk_enqueue_${to_underscore(c.name)}(&cmd_buffer->cmd_queue);
% else:
   VkResult result = vk_enqueue_${to_underscore(c.name)}(&cmd_buffer->cmd_queue,
                                       ${c.call_params(1)});
% endif
   if (unlikely(result != VK_SUCCESS))
      vk_command_buffer_set_error(cmd_buffer, result);
}
% endif

VKAPI_ATTR void VKAPI_CALL
vk_cmd_enqueue_unless_primary_${c.name}(${c.decl_params()})
{
    VK_FROM_HANDLE(vk_command_buffer, cmd_buffer, commandBuffer);

   if (cmd_buffer->level == VK_COMMAND_BUFFER_LEVEL_PRIMARY) {
      const struct vk_device_dispatch_table *disp =
         cmd_buffer->base.device->command_dispatch_table;

      disp->${c.name}(${c.call_params()});
   } else {
      vk_cmd_enqueue_${c.name}(${c.call_params()});
   }
}
% if c.guard is not None:
#endif // ${c.guard}
% endif
% endfor
""")

class CodeBuilder:
    def __init__(self, level):
        self.variable_index = 0
        self.code = ""
        self.level = level

    def add(self, line):
        self.code += "%s%s\n" % ("   " * self.level, line)

    def get_variable_name(self, name):
        self.variable_index += 1
        return "%s%s" % (name, self.variable_index)

def remove_prefix(text, prefix):
    if text.startswith(prefix):
        return text[len(prefix):]
    return text

def remove_suffix(text, suffix):
    if text.endswith(suffix):
        return text[:-len(suffix)]
    return text

def to_underscore(name):
    return remove_prefix(re.sub('([A-Z]+)', r'_\1', name).lower(), '_')

def to_struct_field_name(name):
    return to_underscore(name).replace('cmd_', '')

def to_field_name(name):
    return remove_prefix(to_underscore(name).replace('cmd_', ''), 'p_')

def to_field_decl(decl):
    if 'const*' in decl:
        decl = decl.replace('const*', '*')
    else:
        decl = decl.replace('const ', '')
    [decl, name] = decl.rsplit(' ', 1)
    return decl + ' ' + to_field_name(name)

def to_enum_name(name):
    return "VK_%s" % to_underscore(name).upper()

def to_struct_name(name):
    return "vk_%s" % to_underscore(name)

def get_array_len(param):
    return param.decl[param.decl.find("[") + 1:param.decl.find("]")]

class ParamCategory(Enum):
    ASSIGNABLE = auto()
    FLAT_ARRAY = auto()
    UNSIZED_RAW_POINTER = auto()
    STRING = auto()
    NULL = auto()
    PNEXT = auto()
    STRUCT = auto()

def categorize_param(types, parent_type, param):
    if param.name == 'pNext':
        return ParamCategory.PNEXT if not parent_type or types[parent_type].extended_by else ParamCategory.NULL

    if '[' in param.decl:
        return ParamCategory.FLAT_ARRAY

    if param.type == "void" and not param.len:
        return ParamCategory.UNSIZED_RAW_POINTER

    if param.len == 'null-terminated':
        return ParamCategory.STRING
    
    if "*" not in param.decl:
        return ParamCategory.ASSIGNABLE
    
    return ParamCategory.STRUCT

def get_pnext_copy(builder, types, parent_type, src, dst):
    if not types[parent_type].extended_by:
        return

    builder.add("const VkBaseInStructure *pnext = %s;" % (src))
    builder.add("void **dst_pnext_link = (void **)&%s;" % (dst))
    builder.add("while (pnext) {")
    builder.level += 1
    builder.add("switch ((int32_t)pnext->sType) {")

    for type in types[parent_type].extended_by:
        if type.guard is not None:
            builder.code += "#ifdef %s\n" % (type.guard)

        builder.add("case %s:" % (type.enum))
        builder.level += 1
        member = EntrypointParam(type=type.name, name="", decl="%s *" % (type.name), len=None)
        get_param_copy(builder, types, "pnext", "(*dst_pnext_link)", member, nullable=False)
        builder.add("break;")
        builder.level -= 1

        if type.guard is not None:
            builder.code += "#endif\n"

    builder.add("}")
    builder.add("pnext = pnext->pNext;")
    builder.add("dst_pnext_link = (void **)&((VkBaseOutStructure *)*dst_pnext_link)->pNext;")
    builder.level -= 1
    builder.add("}")

def get_param_copy(builder, types, src_parent_access, dst_parent_access, param, nullable=True, dst_initialized=False, dst_snake_case=False):
    src = src_parent_access + param.name
    dst = dst_parent_access + (to_field_name(param.name) if dst_snake_case else param.name)

    match categorize_param(types, None, param):
        case ParamCategory.ASSIGNABLE:
            builder.add("%s = %s;" % (dst, src))
        case ParamCategory.FLAT_ARRAY:
            builder.add("memcpy(%s, %s, sizeof(*%s) * %s);" % (dst, src, src, get_array_len(param)))
        case ParamCategory.UNSIZED_RAW_POINTER:
            builder.add("%s = (%s)%s;" % (dst, remove_suffix(param.decl.replace("const", ""), param.name), src))
        case ParamCategory.STRING:
            builder.add("%s = linear_strdup(queue->ctx, %s);" % (dst, src))
        case ParamCategory.STRUCT:
            if nullable:
                builder.add("if (%s) {" % (src))
                builder.level += 1

            if param.type == "void":
                size = 1
            else:
                size = "sizeof(%s)" % param.type

            is_ndarray = param.len and "," in param.len
            if param.len and param.len != "struct-ptr" and not is_ndarray:
                size = "%s * %s%s" % (size, src_parent_access, param.len)

            builder.add("%s = linear_alloc_child(queue->ctx, %s);" % (dst, size))
            builder.add("if (%s == NULL) return VK_ERROR_OUT_OF_HOST_MEMORY;" % (dst))
            builder.add("memcpy((void *)%s, %s, %s);" % (dst, src, size))

            if param.type in types:
                needs_member_copy = False
                for member in types[param.type].members:
                    category = categorize_param(types, param.type, member)
                    if category == ParamCategory.PNEXT or category == ParamCategory.STRUCT or category == ParamCategory.STRING:
                        needs_member_copy = True

                if needs_member_copy:
                    tmp_dst_name = builder.get_variable_name("tmp_dst")
                    tmp_src_name = builder.get_variable_name("tmp_src")

                    builder.add("%s *%s = (void *)%s;" % (param.type, tmp_dst_name, dst))
                    builder.add("%s *%s = (void *)%s;" % (param.type, tmp_src_name, src))

                    struct_array_copy = param.len and param.len != "struct-ptr" and param.type != "void"
                    if struct_array_copy:
                        array_index = builder.get_variable_name("i")
                        builder.add("for (uint32_t %s = 0; %s < %s%s; %s++) {" % (array_index, array_index, src_parent_access, param.len, array_index))
                        builder.level += 1
                        prev_tmp_dst_name = tmp_dst_name
                        prev_tmp_src_name = tmp_src_name
                        tmp_dst_name = builder.get_variable_name("tmp_dst")
                        tmp_src_name = builder.get_variable_name("tmp_src")
                        builder.add("%s *%s = %s + %s;" % (param.type, tmp_dst_name, prev_tmp_dst_name, array_index))
                        builder.add("%s *%s = %s + %s;" % (param.type, tmp_src_name, prev_tmp_src_name, array_index))

                    for member in types[param.type].members:
                        category = categorize_param(types, param.type, member)
                        if category == ParamCategory.STRUCT or category == ParamCategory.STRING:
                            get_param_copy(builder, types, "%s->" % (tmp_src_name), "%s->" % (tmp_dst_name), member, dst_initialized=True)
                        elif category == ParamCategory.PNEXT:
                            get_pnext_copy(builder, types, param.type, "%s->pNext" % (tmp_src_name), "%s->pNext" % (tmp_dst_name))

                    if struct_array_copy:
                        builder.level -= 1
                        builder.add("}")

            if nullable:
                builder.level -= 1
                if dst_initialized:
                    builder.add("}")
                else:
                    builder.add("} else {")
                    builder.level += 1
                    builder.add("%s = NULL;" % (dst))
                    builder.level -= 1
                    builder.add("}")
        case ParamCategory.NULL:
            assert False
        case ParamCategory.PNEXT:
            assert False

def get_params_copy(command, types):
    builder = CodeBuilder(1)

    struct_access = "cmd->u.%s." % (to_struct_field_name(command.name))
    for param in command.params[1:]:
        get_param_copy(builder, types, "", struct_access, param, dst_snake_case=True)

    builder.code += "\n"
    builder.add("list_addtail(&cmd->cmd_link, &queue->cmds);")
    builder.add("return VK_SUCCESS;")

    return builder.code

EntrypointType = namedtuple('EntrypointType', 'name enum members extended_by guard')

def get_types_defines(doc):
    """Maps types to extension defines."""
    types_to_defines = {}

    platform_define = {}
    for platform in doc.findall('./platforms/platform'):
        name = platform.attrib['name']
        define = platform.attrib['protect']
        platform_define[name] = define

    for extension in doc.findall('./extensions/extension[@platform]'):
        platform = extension.attrib['platform']
        define = platform_define[platform]

        for types in extension.findall('./require/type'):
            fullname = types.attrib['name']
            types_to_defines[fullname] = define

    return types_to_defines

INTERNAL_STRUCT_EXTENSIONS = {
    "VkRenderingAttachmentInfo": EntrypointType(
        name="VkRenderingAttachmentInitialLayoutInfoMESA",
        enum="VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INITIAL_LAYOUT_INFO_MESA",
        members=[],
        extended_by=[],
        guard=None
    ),
    "VkRenderingInfo": EntrypointType(
        name="VkSampleLocationsInfoEXT",
        enum=None,
        members=None,
        extended_by=None,
        guard=None
    )
}

def get_types(doc, beta, api, types_to_defines):
    """Extract the types from the registry."""
    types = {}

    required = get_all_required(doc, 'type', api, beta)

    for _type in doc.findall('./types/type'):
        if _type.attrib.get('category') != 'struct':
            continue
        if not filter_api(_type, api):
            continue
        if _type.attrib['name'] not in required:
            continue

        members = []
        type_enum = None
        for p in _type.findall('./member'):
            if not filter_api(p, api):
                continue

            mem_type = p.find('./type').text
            mem_name = p.find('./name').text
            mem_decl = ''.join(p.itertext())
            mem_len = p.attrib.get('altlen', p.attrib.get('len', None))
            if mem_len is None and '*' in mem_decl and mem_name != 'pNext':
                mem_len = "struct-ptr"

            member = EntrypointParam(type=mem_type,
                                     name=mem_name,
                                     decl=mem_decl,
                                     len=mem_len)
            members.append(member)

            if mem_name == 'sType':
                type_enum = p.attrib.get('values')
        types[_type.attrib['name']] = EntrypointType(name=_type.attrib['name'], enum=type_enum, members=members, extended_by=[], guard=types_to_defines.get(_type.attrib['name']))

    for _type in doc.findall('./types/type'):
        if _type.attrib.get('category') != 'struct':
            continue
        if not filter_api(_type, api):
            continue
        if _type.attrib['name'] not in required:
            continue
        if _type.attrib.get('structextends') is None:
            continue
        for extended in _type.attrib.get('structextends').split(','):
            if extended not in required:
                continue
            types[extended].extended_by.append(types[_type.attrib['name']])

    for extended in INTERNAL_STRUCT_EXTENSIONS:
        extension = INTERNAL_STRUCT_EXTENSIONS[extended]
        if extension.name in types:
            extension = types[extension.name]
        types[extended].extended_by.append(extension)

    return types

def get_types_from_xml(xml_files, beta, api='vulkan'):
    types = {}

    for filename in xml_files:
        doc = et.parse(filename)
        types.update(get_types(doc, beta, api, get_types_defines(doc)))

    return types

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--out-c', required=True, help='Output C file.')
    parser.add_argument('--out-h', required=True, help='Output H file.')
    parser.add_argument('--beta', required=True, help='Enable beta extensions.')
    parser.add_argument('--xml',
                        help='Vulkan API XML file.',
                        required=True, action='append', dest='xml_files')
    args = parser.parse_args()

    commands = []
    for e in get_entrypoints_from_xml(args.xml_files, args.beta):
        if e.name.startswith('Cmd') and \
           not e.alias:
            commands.append(e)

    types = get_types_from_xml(args.xml_files, args.beta)

    assert os.path.dirname(args.out_c) == os.path.dirname(args.out_h)

    environment = {
        'header': os.path.basename(args.out_h),
        'commands': commands,
        'filename': os.path.basename(__file__),
        'to_underscore': to_underscore,
        'to_struct_field_name': to_struct_field_name,
        'to_field_name': to_field_name,
        'to_field_decl': to_field_decl,
        'to_enum_name': to_enum_name,
        'to_struct_name': to_struct_name,
        'get_params_copy': get_params_copy,
        'types': types,
        'manual_commands': MANUAL_COMMANDS,
        'no_enqueue_commands': NO_ENQUEUE_COMMANDS,
        'remove_suffix': remove_suffix,
    }

    try:
        with open(args.out_h, 'w', encoding='utf-8') as f:
            guard = os.path.basename(args.out_h).replace('.', '_').upper()
            f.write(TEMPLATE_H.render(guard=guard, **environment))
        with open(args.out_c, 'w', encoding='utf-8') as f:
            f.write(TEMPLATE_C.render(**environment))
    except Exception:
        # In the event there's an error, this imports some helpers from mako
        # to print a useful stack trace and prints it, then exits with
        # status 1, if python is run with debug; otherwise it just raises
        # the exception
        import sys
        from mako import exceptions
        print(exceptions.text_error_template().render(), file=sys.stderr)
        sys.exit(1)

if __name__ == '__main__':
    main()
