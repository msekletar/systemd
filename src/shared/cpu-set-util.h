/* SPDX-License-Identifier: LGPL-2.1+ */
#pragma once

#include <sched.h>

#include "macro.h"

#ifdef __NCPUBITS
#define CPU_SIZE_TO_NUM(n) ((n) * __NCPUBITS)
#else
#define CPU_SIZE_TO_NUM(n) ((n) * sizeof(cpu_set_t) * 8)
#endif

DEFINE_TRIVIAL_CLEANUP_FUNC(cpu_set_t*, CPU_FREE);
#define _cleanup_cpu_free_ _cleanup_(CPU_FREEp)

static inline cpu_set_t* cpu_set_mfree(cpu_set_t *p) {
        if (p)
                CPU_FREE(p);
        return NULL;
}

cpu_set_t* cpu_set_malloc(unsigned *ncpus);

int parse_cpu_set_internal(const char *rvalue, cpu_set_t **cpu_set, bool warn, const char *unit, const char *filename, unsigned line, const char *lvalue);

static inline int parse_cpu_set_and_warn(const char *rvalue, cpu_set_t **cpu_set, const char *unit, const char *filename, unsigned line, const char *lvalue) {
        assert(lvalue);

        return parse_cpu_set_internal(rvalue, cpu_set, true, unit, filename, line, lvalue);
}

static inline int parse_cpu_set(const char *rvalue, cpu_set_t **cpu_set){
        return parse_cpu_set_internal(rvalue, cpu_set, false, NULL, NULL, 0, NULL);
}

typedef enum NUMAMemPolicyType {
        NUMA_MEM_POLICY_TYPE_DEFAULT,
        NUMA_MEM_POLICY_TYPE_PREFER,
        NUMA_MEM_POLICY_TYPE_BIND,
        NUMA_MEM_POLICY_TYPE_INTERLEAVE,
        _NUMA_MEM_POLICY_TYPE_MAX,
        _NUMA_MEM_POLICY_TYPE_INVALID = -1,
} NUMAMemPolicyType;

typedef struct NUMAMemPolicy {
        NUMAMemPolicyType type;
        cpu_set_t *nodemask;
        unsigned long maxnode;
} NUMAMemPolicy;

static inline void free_numap(NUMAMemPolicy **p) {
        if (*p && (*p)->nodemask)
                (*p)->nodemask = cpu_set_mfree((*p)->nodemask);
        free(*p);
        *p = NULL;
}

#define _cleanup_numa_ _cleanup_(free_numap)

int parse_numa_mem_policy_from_string(const char *line, NUMAMemPolicy **ret);
int set_numa_mem_policy(const NUMAMemPolicy *policy);
unsigned long num_numa_nodes(void);

int cpu_set_to_string_alloc(cpu_set_t *nodes, size_t size, char **ret);

const char* numa_mem_policy_type_to_string(NUMAMemPolicyType i) _const_;
NUMAMemPolicyType numa_mem_policy_type_from_string(const char *s) _pure_;
