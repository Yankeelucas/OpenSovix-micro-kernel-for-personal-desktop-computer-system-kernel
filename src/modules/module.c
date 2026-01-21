// SPDX-License-Identifier: GPL-3.0-or-later
// module.c - 内核模块管理系统
#include "kernel.h"
#include "module.h"
#include "memory.h"

// 模块管理内部结构
typedef struct ModuleEntry {
    char name[32];
    void* base_address;
    u32 size;
    ModuleStatus status;
    ModuleInfo info;
    ModuleExportTable* exports;
    
    // 依赖关系
    struct ModuleEntry** dependencies;
    u32 dep_count;
    
    // 链表
    struct ModuleEntry* next;
} ModuleEntry;

// 全局模块链表
static ModuleEntry* module_list = NULL;
static u32 module_count = 0;
static u32 next_module_id = 1;

// 模块系统初始化
ErrorCode module_system_init(void) {
    kprintf("Initializing module system...\n");
    
    // 创建模块管理内存池
    MemoryPool* pool = mempool_create("module_pool", MEM_POOL_MEDIUM, 64 * KB);
    if (!pool) {
        kprintf("  ERROR: Failed to create module memory pool\n");
        return ERR_NO_MEMORY;
    }
    
    kprintf("  Module system ready (max %d modules)\n", MAX_MODULES);
    return ERR_SUCCESS;
}

// 加载模块
ErrorCode module_load(const char* filename, ModuleInfo* out_info) {
    kprintf("Loading module: %s\n", filename);
    
    // 检查模块是否已加载
    ModuleEntry* existing = module_find(filename);
    if (existing) {
        kprintf("  Module already loaded\n");
        if (out_info) *out_info = existing->info;
        return ERR_EXISTS;
    }
    
    // 1. 从存储加载模块文件
    u32 file_size;
    void* module_data = filesystem_load_file(filename, &file_size);
    if (!module_data) {
        kprintf("  ERROR: Failed to load module file\n");
        return ERR_NOT_FOUND;
    }
    
    // 2. 验证模块头
    ModuleHeader* header = (ModuleHeader*)module_data;
    if (header->magic != MODULE_MAGIC) {
        kprintf("  ERROR: Invalid module magic\n");
        memory_free(module_data);
        return ERR_INVALID_MODULE;
    }
    
    // 3. 分配内存给模块
    void* module_base = memory_alloc_module(header->text_size + header->data_size);
    if (!module_base) {
        kprintf("  ERROR: Failed to allocate module memory\n");
        memory_free(module_data);
        return ERR_NO_MEMORY;
    }
    
    // 4. 复制代码和数据
    memcpy(module_base, module_data + sizeof(ModuleHeader), header->text_size);
    void* data_section = module_base + header->text_size;
    memcpy(data_section, 
           module_data + sizeof(ModuleHeader) + header->text_size,
           header->data_size);
    
    // 5. 创建模块入口
    ModuleEntry* entry = (ModuleEntry*)memory_alloc(sizeof(ModuleEntry));
    if (!entry) {
        kprintf("  ERROR: Failed to allocate module entry\n");
        memory_free(module_base);
        memory_free(module_data);
        return ERR_NO_MEMORY;
    }
    
    // 6. 填充模块信息
    memset(entry, 0, sizeof(ModuleEntry));
    strncpy(entry->name, filename, sizeof(entry->name) - 1);
    entry->base_address = module_base;
    entry->size = header->text_size + header->data_size;
    entry->status = MODULE_STATUS_LOADING;
    
    // 7. 定位导出表
    ModuleExportTable* exports = (ModuleExportTable*)(module_base + header->export_offset);
    entry->exports = exports;
    entry->info = exports->info;
    
    // 8. 初始化模块
    kprintf("  Initializing module: %s\n", entry->info.name);
    ErrorCode init_result = exports->init(NULL);
    if (init_result != ERR_SUCCESS) {
        kprintf("  ERROR: Module initialization failed: %d\n", init_result);
        memory_free(module_base);
        memory_free(entry);
        memory_free(module_data);
        return init_result;
    }
    
    // 9. 添加到模块链表
    entry->status = MODULE_STATUS_ACTIVE;
    entry->next = module_list;
    module_list = entry;
    module_count++;
    
    // 10. 返回模块信息
    if (out_info) *out_info = entry->info;
    
    memory_free(module_data);
    kprintf("  Module loaded successfully\n");
    return ERR_SUCCESS;
}

// 卸载模块
ErrorCode module_unload(const char* name) {
    kprintf("Unloading module: %s\n", name);
    
    ModuleEntry* prev = NULL;
    ModuleEntry* curr = module_list;
    
    // 查找模块
    while (curr) {
        if (strcmp(curr->name, name) == 0) {
            break;
        }
        prev = curr;
        curr = curr->next;
    }
    
    if (!curr) {
        kprintf("  Module not found\n");
        return ERR_NOT_FOUND;
    }
    
    // 检查模块状态
    if (curr->info.type == MODULE_TYPE_CORE) {
        kprintf("  ERROR: Cannot unload core module\n");
        return ERR_PERMISSION;
    }
    
    // 检查依赖关系
    if (module_check_dependencies(curr)) {
        kprintf("  ERROR: Module has dependent modules\n");
        return ERR_BUSY;
    }
    
    // 调用模块的退出函数
    if (curr->exports->exit) {
        ErrorCode exit_result = curr->exports->exit();
        if (exit_result != ERR_SUCCESS) {
            kprintf("  WARNING: Module exit returned error: %d\n", exit_result);
        }
    }
    
    // 从链表中移除
    if (prev) {
        prev->next = curr->next;
    } else {
        module_list = curr->next;
    }
    
    // 释放模块内存
    memory_free_module(curr->base_address, curr->size);
    memory_free(curr);
    module_count--;
    
    kprintf("  Module unloaded successfully\n");
    return ERR_SUCCESS;
}

// 查找模块
ModuleEntry* module_find(const char* name) {
    ModuleEntry* curr = module_list;
    while (curr) {
        if (strcmp(curr->name, name) == 0) {
            return curr;
        }
        curr = curr->next;
    }
    return NULL;
}

// 查询模块信息
ErrorCode module_query(const char* name, ModuleInfo* info) {
    ModuleEntry* module = module_find(name);
    if (!module) {
        return ERR_NOT_FOUND;
    }
    
    if (info) *info = module->info;
    return ERR_SUCCESS;
}

// 调用模块函数
void* module_call(const char* module_name, u32 function_index, void* params) {
    ModuleEntry* module = module_find(module_name);
    if (!module) {
        kprintf("Module not found: %s\n", module_name);
        return NULL;
    }
    
    if (module->status != MODULE_STATUS_ACTIVE) {
        kprintf("Module not active: %s\n", module_name);
        return NULL;
    }
    
    if (function_index >= 15) {
        kprintf("Invalid function index: %d\n", function_index);
        return NULL;
    }
    
    void* function = module->exports->functions[function_index];
    if (!function) {
        kprintf("Function not implemented: index %d\n", function_index);
        return NULL;
    }
    
    // 调用函数
    typedef void* (*ModuleFunc)(void*);
    ModuleFunc func = (ModuleFunc)function;
    return func(params);
}

// 列出所有模块
void module_list_all(void) {
    kprintf("\n=== Loaded Modules (%d) ===\n", module_count);
    
    ModuleEntry* curr = module_list;
    while (curr) {
        kprintf("%-20s %-10s v%-8s %s\n",
                curr->info.name,
                module_type_to_string(curr->info.type),
                curr->info.version,
                curr->status == MODULE_STATUS_ACTIVE ? "[ACTIVE]" : "[INACTIVE]");
        curr = curr->next;
    }
}

// 检查模块依赖
bool module_check_dependencies(ModuleEntry* module) {
    // 简化实现：检查是否有其他模块依赖此模块
    ModuleEntry* curr = module_list;
    while (curr) {
        if (curr == module) {
            curr = curr->next;
            continue;
        }
        
        // 检查依赖关系
        for (u32 i = 0; i < curr->dep_count; i++) {
            if (curr->dependencies[i] == module) {
                return true;
            }
        }
        
        curr = curr->next;
    }
    return false;
}

// 模块健康检查
void module_check_health(void) {
    ModuleEntry* curr = module_list;
    while (curr) {
        // 这里可以添加健康检查逻辑
        // 例如：检查模块内存完整性、验证函数指针等
        curr = curr->next;
    }
}

// 类型转换为字符串
const char* module_type_to_string(ModuleType type) {
    switch (type) {
        case MODULE_TYPE_CORE:      return "CORE";
        case MODULE_TYPE_MEMORY:    return "MEMORY";
        case MODULE_TYPE_FILESYSTEM:return "FS";
        case MODULE_TYPE_DEVICE:    return "DEVICE";
        case MODULE_TYPE_NETWORK:   return "NETWORK";
        case MODULE_TYPE_SECURITY:  return "SECURITY";
        case MODULE_TYPE_UTILITY:   return "UTILITY";
        default:                    return "UNKNOWN";
    }
}

// 模块管理头文件
typedef struct {
    u32 magic;
    u32 version;
    u32 text_size;
    u32 data_size;
    u32 bss_size;
    u32 export_offset;
    u32 import_offset;
    u32 checksum;
} ModuleHeader;

#define MODULE_MAGIC 0x4D4F4455  // "MODU"
