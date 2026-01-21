// SPDX-License-Identifier: GPL-3.0-or-later
// syscall.c - 系统调用处理
#include "kernel.h"
#include "syscall.h"
#include "process.h"
#include "module.h"

// 系统调用处理表
static SyscallHandler syscall_table[SYS_MAX];

// 系统调用初始化
void syscall_init(void) {
    kprintf("Initializing system calls...\n");
    
    // 初始化系统调用表
    memset(syscall_table, 0, sizeof(syscall_table));
    
    // 注册基本系统调用
    syscall_register(SYS_EXIT, syscall_exit);
    syscall_register(SYS_GETPID, syscall_getpid);
    syscall_register(SYS_GETTIME, syscall_gettime);
    
    // 注册内存管理调用
    syscall_register(SYS_ALLOC, syscall_alloc);
    syscall_register(SYS_FREE, syscall_free);
    syscall_register(SYS_MEMINFO, syscall_meminfo);
    syscall_register(SYS_MPOOL_CREATE, syscall_mpool_create);
    syscall_register(SYS_MPOOL_DESTROY, syscall_mpool_destroy);
    
    // 注册IPC调用
    syscall_register(SYS_IPC_SEND, syscall_ipc_send);
    syscall_register(SYS_IPC_RECEIVE, syscall_ipc_receive);
    syscall_register(SYS_IPC_QUEUE_CREATE, syscall_ipc_queue_create);
    syscall_register(SYS_IPC_QUEUE_DELETE, syscall_ipc_queue_delete);
    
    // 注册模块管理调用
    syscall_register(SYS_MODULE_LOAD, syscall_module_load);
    syscall_register(SYS_MODULE_UNLOAD, syscall_module_unload);
    syscall_register(SYS_MODULE_QUERY, syscall_module_query);
    syscall_register(SYS_MODULE_CALL, syscall_module_call);
    
    kprintf("  System calls ready (%d registered)\n", SYS_MAX);
}

// 注册系统调用
ErrorCode syscall_register(SyscallNumber num, SyscallHandler handler) {
    if (num >= SYS_MAX) {
        return ERR_INVALID_ARG;
    }
    
    syscall_table[num] = handler;
    return ERR_SUCCESS;
}

// 系统调用处理入口（由中断调用）
void syscall_handler(SyscallParams* params) {
    if (!params) {
        return;
    }
    
    u32 num = params->syscall_num;
    
    // 验证系统调用号
    if (num >= SYS_MAX || !syscall_table[num]) {
        params->result = ERR_NOT_IMPLEMENTED;
        return;
    }
    
    // 调用处理函数
    params->result = syscall_table[num](params);
}

// 系统调用处理函数实现

// 进程退出
static u32 syscall_exit(SyscallParams* params) {
    Process* proc = process_get_current();
    if (proc) {
        process_exit_handler();
    }
    return ERR_SUCCESS;
}

// 获取进程ID
static u32 syscall_getpid(SyscallParams* params) {
    Process* proc = process_get_current();
    return proc ? proc->pid : 0;
}

// 获取系统时间
static u32 syscall_gettime(SyscallParams* params) {
    return (u32)system_ticks;
}

// 内存分配
static u32 syscall_alloc(SyscallParams* params) {
    u32 size = params->arg1;
    u32 pool_id = params->arg2;
    
    // 查找内存池
    // 这里简化实现
    void* ptr = memory_alloc(size);
    return (u32)ptr;
}

// 内存释放
static u32 syscall_free(SyscallParams* params) {
    void* ptr = (void*)params->arg1;
    return memory_free(ptr);
}

// 内存信息
static u32 syscall_meminfo(SyscallParams* params) {
    // 返回内存统计信息
    // 这里简化实现
    return memory_get_free();
}

// 创建内存池
static u32 syscall_mpool_create(SyscallParams* params) {
    const char* name = (const char*)params->arg1;
    u32 type = params->arg2;
    u32 size = params->arg3;
    
    // 调用内存管理模块
    MemoryPool* pool = mempool_create(name, type, size);
    return pool ? pool->id : ERR_GENERIC;
}

// 销毁内存池
static u32 syscall_mpool_destroy(SyscallParams* params) {
    u32 pool_id = params->arg1;
    
    // 查找并销毁内存池
    // 这里简化实现
    return ERR_SUCCESS;
}

// IPC发送消息
static u32 syscall_ipc_send(SyscallParams* params) {
    u32 queue_id = params->arg1;
    void* data = (void*)params->arg2;
    u32 size = params->arg3;
    u32 timeout = params->arg4;
    
    return ipc_send(queue_id, data, size, timeout);
}

// IPC接收消息
static u32 syscall_ipc_receive(SyscallParams* params) {
    u32 queue_id = params->arg1;
    void* buffer = (void*)params->arg2;
    u32 buffer_size = params->arg3;
    u32* actual_size = (u32*)params->arg4;
    u32 timeout = params->arg5;
    
    return ipc_receive(queue_id, buffer, buffer_size, actual_size, timeout);
}

// 加载模块
static u32 syscall_module_load(SyscallParams* params) {
    const char* filename = (const char*)params->arg1;
    ModuleInfo* info = (ModuleInfo*)params->arg2;
    
    return module_load(filename, info);
}

// 卸载模块
static u32 syscall_module_unload(SyscallParams* params) {
    const char* name = (const char*)params->arg1;
    return module_unload(name);
}

// 查询模块
static u32 syscall_module_query(SyscallParams* params) {
    const char* name = (const char*)params->arg1;
    ModuleInfo* info = (ModuleInfo*)params->arg2;
    
    return module_query(name, info);
}

// 调用模块函数
static u32 syscall_module_call(SyscallParams* params) {
    const char* module_name = (const char*)params->arg1;
    u32 function_index = params->arg2;
    void* func_params = (void*)params->arg3;
    void** result = (void**)params->arg4;
    
    void* ret = module_call(module_name, function_index, func_params);
    if (result) *result = ret;
    
    return ret ? ERR_SUCCESS : ERR_GENERIC;
}