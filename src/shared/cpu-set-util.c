/* SPDX-License-Identifier: LGPL-2.1+ */

#include <errno.h>
#include <stddef.h>
#include <syslog.h>

#include "alloc-util.h"
#include "cpu-set-util.h"
#include "errno-util.h"
#include "extract-word.h"
#include "glob-util.h"
#include "log.h"
#include "macro.h"
#include "missing_syscall.h"
#include "parse-util.h"
#include "stat-util.h"
#include "string-util.h"
#include "string-table.h"
#include "strv.h"
#include "util.h"

cpu_set_t* cpu_set_malloc(unsigned *ncpus) {
        cpu_set_t *c;
        unsigned n = 1024;

        /* Allocates the cpuset in the right size */

        for (;;) {
                c = CPU_ALLOC(n);
                if (!c)
                        return NULL;

                if (sched_getaffinity(0, CPU_ALLOC_SIZE(n), c) >= 0) {
                        CPU_ZERO_S(CPU_ALLOC_SIZE(n), c);

                        if (ncpus)
                                *ncpus = n;

                        return c;
                }

                CPU_FREE(c);

                if (errno != EINVAL)
                        return NULL;

                n *= 2;
        }
}

int parse_cpu_set_internal(
                const char *rvalue,
                cpu_set_t **cpu_set,
                bool warn,
                const char *unit,
                const char *filename,
                unsigned line,
                const char *lvalue) {

        _cleanup_cpu_free_ cpu_set_t *c = NULL;
        const char *p = rvalue;
        unsigned ncpus = 0;

        assert(rvalue);

        for (;;) {
                _cleanup_free_ char *word = NULL;
                unsigned cpu, cpu_lower, cpu_upper;
                int r;

                r = extract_first_word(&p, &word, WHITESPACE ",", EXTRACT_QUOTES);
                if (r == -ENOMEM)
                        return warn ? log_oom() : -ENOMEM;
                if (r < 0)
                        return warn ? log_syntax(unit, LOG_ERR, filename, line, r, "Invalid value for %s: %s", lvalue, rvalue) : r;
                if (r == 0)
                        break;

                if (!c) {
                        c = cpu_set_malloc(&ncpus);
                        if (!c)
                                return warn ? log_oom() : -ENOMEM;
                }

                r = parse_range(word, &cpu_lower, &cpu_upper);
                if (r < 0)
                        return warn ? log_syntax(unit, LOG_ERR, filename, line, r, "Failed to parse CPU affinity '%s'", word) : r;
                if (cpu_lower >= ncpus || cpu_upper >= ncpus)
                        return warn ? log_syntax(unit, LOG_ERR, filename, line, EINVAL, "CPU out of range '%s' ncpus is %u", word, ncpus) : -EINVAL;

                if (cpu_lower > cpu_upper) {
                        if (warn)
                                log_syntax(unit, LOG_WARNING, filename, line, 0, "Range '%s' is invalid, %u > %u, ignoring", word, cpu_lower, cpu_upper);
                        continue;
                }

                for (cpu = cpu_lower; cpu <= cpu_upper; cpu++)
                        CPU_SET_S(cpu, CPU_ALLOC_SIZE(ncpus), c);
        }

        /* On success, sets *cpu_set and returns ncpus for the system. */
        if (c)
                *cpu_set = TAKE_PTR(c);

        return (int) ncpus;
}

int parse_numa_mem_policy_from_string(const char *line, NUMAMemPolicy **ret) {
        int r;
        _cleanup_numa_ NUMAMemPolicy *policy = NULL;
        NUMAMemPolicyType type;
        _cleanup_free_ char *val = NULL;
        char *s;
        long prefered = -1;
        _cleanup_cpu_free_ cpu_set_t *nodes = NULL;
        unsigned long n_nodes;

        assert(line);
        assert(ret);

        if (isempty(line))
                return -EINVAL;

        val = strdup(line);
        if (!val) {
                r = -ENOMEM;
                goto fail;
        }

        s = strchr(val, ',');
        if (s)
                *s++ = '\0';

        policy = new0(NUMAMemPolicy, 1);
        if (!policy) {
                r = -ENOMEM;
                goto fail;
        }

        type = numa_mem_policy_type_from_string(val);
        if (type < 0) {
                r = -ENOMEM;
                goto fail;
        }

        if (type != NUMA_MEM_POLICY_TYPE_DEFAULT && isempty(s)) {
                r = -EINVAL;
                goto fail;
        }

        n_nodes = num_numa_nodes();
        if (n_nodes == 0)
                return -EOPNOTSUPP;

        nodes = CPU_ALLOC(n_nodes);
        if (!nodes) {
                r = -ENOMEM;
                goto fail;
        }
        CPU_ZERO_S(CPU_ALLOC_SIZE(n_nodes), nodes);

        if (type == NUMA_MEM_POLICY_TYPE_BIND || type == NUMA_MEM_POLICY_TYPE_INTERLEAVE) {
                while (true) {
                        _cleanup_free_ char *word = NULL;
                        unsigned node, lower, upper;

                        r = extract_first_word((const char **)&s, &word, WHITESPACE ",", EXTRACT_QUOTES);
                        if (r == 0)
                                break;
                        if (r == -ENOMEM)
                                goto fail;
                        if (r < 0) {
                                r = -EINVAL;
                                goto fail;
                        }

                        r = parse_range(word, &lower, &upper);
                        if (r < 0 ||
                            (lower >= n_nodes || upper >= n_nodes) ||
                            (lower > upper)) {
                                r = -ERANGE;
                                goto fail;
                        }

                        for (node = lower; node <= upper; node++)
                                CPU_SET_S(node, CPU_ALLOC_SIZE(n_nodes), nodes);
                }
                policy->nodemask = TAKE_PTR(nodes);
                policy->maxnode = n_nodes;

        } else if (type == NUMA_MEM_POLICY_TYPE_PREFER) {
                PROTECT_ERRNO;

                errno = 0;
                prefered = strtol(s, NULL, 10);
                if (prefered < 0 || errno == ERANGE) {
                        r = -EINVAL;
                        goto fail;
                }

                CPU_SET_S(prefered, CPU_ALLOC_SIZE(n_nodes), nodes);
                policy->nodemask = TAKE_PTR(nodes);
                policy->maxnode = n_nodes;
        }

        policy->type = type;

        *ret = TAKE_PTR(policy);

        return 0;

fail:
        return r;
}

static int cpuset_to_mempolicy(cpu_set_t *nodes, unsigned long maxnode, unsigned long **nodemask) {
        const unsigned ULONG_BITS = 8 * sizeof(unsigned long);
        size_t i;
        _cleanup_free_ unsigned long *mask = NULL;

        assert(nodes);
        assert(nodemask);

        mask = new0(unsigned long, (maxnode / ULONG_BITS) + ((maxnode % ULONG_BITS) == 0) ? 0 : 1);
        if (!mask)
                return -ENOMEM;

        for (i = 0; i < maxnode; i++) {
                size_t idx, bit;

                if (CPU_ISSET_S(i, CPU_ALLOC_SIZE(maxnode), nodes)) {
                        idx = i / ULONG_BITS;
                        bit = i % ULONG_BITS;

                        mask[idx] |= 1 << bit;
                }
        }

        *nodemask = TAKE_PTR(mask);

        return 0;
}

int set_numa_mem_policy(const NUMAMemPolicy *policy) {
        int mode, r;
        _cleanup_free_ unsigned long *nodemask = NULL;

        assert(policy);

        if (get_mempolicy(NULL, NULL, 0, 0, 0) < 0 && errno == ENOSYS)
                return -EOPNOTSUPP;

        if (policy->nodemask) {
                r = cpuset_to_mempolicy(policy->nodemask, policy->maxnode, &nodemask);
                if (r < 0)
                        return r;
        }

        switch (policy->type) {
        case NUMA_MEM_POLICY_TYPE_PREFER:
                mode = MPOL_PREFERRED;
                break;
        case NUMA_MEM_POLICY_TYPE_BIND:
                mode = MPOL_BIND;
                break;
        case NUMA_MEM_POLICY_TYPE_INTERLEAVE:
                mode = MPOL_INTERLEAVE;
                break;
        case NUMA_MEM_POLICY_TYPE_DEFAULT:
                mode = MPOL_DEFAULT;
                break;
        default:
                assert_not_reached("Invalid NUMA policy");
        }

        r = set_mempolicy(mode, nodemask , policy->maxnode + 1);
        if (r < 0)
                return r;

        return 0;
}

unsigned long num_numa_nodes(void) {
        _cleanup_strv_free_ char **nodes = NULL;
        char **n;
        unsigned long n_nodes = 0;
        int r;

        r = glob_extend(&nodes, "/sys/devices/system/node/node*");
        if (r < 0)
                return 0;

        STRV_FOREACH(n, nodes)
                if (is_dir(*n, false))
                        n_nodes++;

        return n_nodes;
}

int cpu_set_to_string_alloc(cpu_set_t *nodes, size_t size, char **s) {
        int r;
        _cleanup_free_ char *str = NULL;
        size_t allocated = 0, len = 0, i, ncpus;

        assert(nodes);
        assert(s);

        ncpus = CPU_SIZE_TO_NUM(size);

        for (i = 0; i < ncpus; i++) {
                _cleanup_free_ char *p = NULL;
                size_t add;

                if (!CPU_ISSET_S(i, size, nodes))
                        continue;

                r = asprintf(&p, "%zu", i);
                if (r < 0)
                        return -ENOMEM;

                add = strlen(p);

                if (!GREEDY_REALLOC(str, allocated, len + add + 2))
                        return -ENOMEM;

                strcpy(mempcpy(str + len, p, add), " ");
                len += add + 1;
        }

        if (len != 0)
                str[len - 1] = '\0';

        *s = TAKE_PTR(str);
        return 0;
}

static const char* const numa_mem_policy_type_table[_NUMA_MEM_POLICY_TYPE_MAX] = {
        [NUMA_MEM_POLICY_TYPE_DEFAULT] = "default",
        [NUMA_MEM_POLICY_TYPE_PREFER] = "prefer",
        [NUMA_MEM_POLICY_TYPE_BIND] = "bind",
        [NUMA_MEM_POLICY_TYPE_INTERLEAVE] = "interleave"
};

DEFINE_STRING_TABLE_LOOKUP(numa_mem_policy_type, NUMAMemPolicyType);
