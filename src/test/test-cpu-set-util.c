/* SPDX-License-Identifier: LGPL-2.1+ */

#include "alloc-util.h"
#include "cpu-set-util.h"
#include "macro.h"
#include "string-util.h"

static void test_parse_cpu_set(void) {
        _cleanup_numa_ NUMAMemPolicy *p = NULL;
        cpu_set_t *c = NULL;
        int cpu, ncpus;

        /* Simple range (from CPUAffinity example) */
        ncpus = parse_cpu_set_and_warn("1 2", &c, NULL, "fake", 1, "CPUAffinity");
        assert_se(ncpus >= 1024);
        assert_se(CPU_ISSET_S(1, CPU_ALLOC_SIZE(ncpus), c));
        assert_se(CPU_ISSET_S(2, CPU_ALLOC_SIZE(ncpus), c));
        assert_se(CPU_COUNT_S(CPU_ALLOC_SIZE(ncpus), c) == 2);
        c = cpu_set_mfree(c);

        /* A more interesting range */
        ncpus = parse_cpu_set_and_warn("0 1 2 3 8 9 10 11", &c, NULL, "fake", 1, "CPUAffinity");
        assert_se(ncpus >= 1024);
        assert_se(CPU_COUNT_S(CPU_ALLOC_SIZE(ncpus), c) == 8);
        for (cpu = 0; cpu < 4; cpu++)
                assert_se(CPU_ISSET_S(cpu, CPU_ALLOC_SIZE(ncpus), c));
        for (cpu = 8; cpu < 12; cpu++)
                assert_se(CPU_ISSET_S(cpu, CPU_ALLOC_SIZE(ncpus), c));
        c = cpu_set_mfree(c);

        /* Quoted strings */
        ncpus = parse_cpu_set_and_warn("8 '9' 10 \"11\"", &c, NULL, "fake", 1, "CPUAffinity");
        assert_se(ncpus >= 1024);
        assert_se(CPU_COUNT_S(CPU_ALLOC_SIZE(ncpus), c) == 4);
        for (cpu = 8; cpu < 12; cpu++)
                assert_se(CPU_ISSET_S(cpu, CPU_ALLOC_SIZE(ncpus), c));
        c = cpu_set_mfree(c);

        /* Use commas as separators */
        ncpus = parse_cpu_set_and_warn("0,1,2,3 8,9,10,11", &c, NULL, "fake", 1, "CPUAffinity");
        assert_se(ncpus >= 1024);
        assert_se(CPU_COUNT_S(CPU_ALLOC_SIZE(ncpus), c) == 8);
        for (cpu = 0; cpu < 4; cpu++)
                assert_se(CPU_ISSET_S(cpu, CPU_ALLOC_SIZE(ncpus), c));
        for (cpu = 8; cpu < 12; cpu++)
                assert_se(CPU_ISSET_S(cpu, CPU_ALLOC_SIZE(ncpus), c));
        c = cpu_set_mfree(c);

        /* Commas with spaces (and trailing comma, space) */
        ncpus = parse_cpu_set_and_warn("0, 1, 2, 3, 4, 5, 6, 7, ", &c, NULL, "fake", 1, "CPUAffinity");
        assert_se(ncpus >= 1024);
        assert_se(CPU_COUNT_S(CPU_ALLOC_SIZE(ncpus), c) == 8);
        for (cpu = 0; cpu < 8; cpu++)
                assert_se(CPU_ISSET_S(cpu, CPU_ALLOC_SIZE(ncpus), c));
        c = cpu_set_mfree(c);

        /* Ranges */
        ncpus = parse_cpu_set_and_warn("0-3,8-11", &c, NULL, "fake", 1, "CPUAffinity");
        assert_se(ncpus >= 1024);
        assert_se(CPU_COUNT_S(CPU_ALLOC_SIZE(ncpus), c) == 8);
        for (cpu = 0; cpu < 4; cpu++)
                assert_se(CPU_ISSET_S(cpu, CPU_ALLOC_SIZE(ncpus), c));
        for (cpu = 8; cpu < 12; cpu++)
                assert_se(CPU_ISSET_S(cpu, CPU_ALLOC_SIZE(ncpus), c));
        c = cpu_set_mfree(c);

        /* Ranges with trailing comma, space */
        ncpus = parse_cpu_set_and_warn("0-3  8-11, ", &c, NULL, "fake", 1, "CPUAffinity");
        assert_se(ncpus >= 1024);
        assert_se(CPU_COUNT_S(CPU_ALLOC_SIZE(ncpus), c) == 8);
        for (cpu = 0; cpu < 4; cpu++)
                assert_se(CPU_ISSET_S(cpu, CPU_ALLOC_SIZE(ncpus), c));
        for (cpu = 8; cpu < 12; cpu++)
                assert_se(CPU_ISSET_S(cpu, CPU_ALLOC_SIZE(ncpus), c));
        c = cpu_set_mfree(c);

        /* Negative range (returns empty cpu_set) */
        ncpus = parse_cpu_set_and_warn("3-0", &c, NULL, "fake", 1, "CPUAffinity");
        assert_se(ncpus >= 1024);
        assert_se(CPU_COUNT_S(CPU_ALLOC_SIZE(ncpus), c) == 0);
        c = cpu_set_mfree(c);

        /* Overlapping ranges */
        ncpus = parse_cpu_set_and_warn("0-7 4-11", &c, NULL, "fake", 1, "CPUAffinity");
        assert_se(ncpus >= 1024);
        assert_se(CPU_COUNT_S(CPU_ALLOC_SIZE(ncpus), c) == 12);
        for (cpu = 0; cpu < 12; cpu++)
                assert_se(CPU_ISSET_S(cpu, CPU_ALLOC_SIZE(ncpus), c));
        c = cpu_set_mfree(c);

        /* Mix ranges and individual CPUs */
        ncpus = parse_cpu_set_and_warn("0,1 4-11", &c, NULL, "fake", 1, "CPUAffinity");
        assert_se(ncpus >= 1024);
        assert_se(CPU_COUNT_S(CPU_ALLOC_SIZE(ncpus), c) == 10);
        assert_se(CPU_ISSET_S(0, CPU_ALLOC_SIZE(ncpus), c));
        assert_se(CPU_ISSET_S(1, CPU_ALLOC_SIZE(ncpus), c));
        for (cpu = 4; cpu < 12; cpu++)
                assert_se(CPU_ISSET_S(cpu, CPU_ALLOC_SIZE(ncpus), c));
        c = cpu_set_mfree(c);

        /* Garbage */
        ncpus = parse_cpu_set_and_warn("0 1 2 3 garbage", &c, NULL, "fake", 1, "CPUAffinity");
        assert_se(ncpus < 0);
        assert_se(!c);

        /* Range with garbage */
        ncpus = parse_cpu_set_and_warn("0-3 8-garbage", &c, NULL, "fake", 1, "CPUAffinity");
        assert_se(ncpus < 0);
        assert_se(!c);

        /* Empty string */
        c = NULL;
        ncpus = parse_cpu_set_and_warn("", &c, NULL, "fake", 1, "CPUAffinity");
        assert_se(ncpus == 0);  /* empty string returns 0 */
        assert_se(!c);

        /* Runaway quoted string */
        ncpus = parse_cpu_set_and_warn("0 1 2 3 \"4 5 6 7 ", &c, NULL, "fake", 1, "CPUAffinity");
        assert_se(ncpus < 0);
        assert_se(!c);
}

static void test_parse_numa_mem_policy(void) {
        _cleanup_numa_ NUMAMemPolicy *p = NULL;
        int r;

        /* Empty policy */
        r = parse_numa_mem_policy_from_string("", &p);
        assert_se(r < 0);
        assert_se(!p);

        /* Unknown policy */
        r = parse_numa_mem_policy_from_string("unknown", &p);
        assert_se(r < 0);
        assert_se(!p);

        /* Default policy */
        r = parse_numa_mem_policy_from_string("local", &p);
        assert_se(r == 0);
        assert_se(p && p->type == NUMA_MEM_POLICY_TYPE_DEFAULT);
        free_numap(&p);

        /* Default policy with trailing garbage */
        r = parse_numa_mem_policy_from_string("local!!!!!0-1", &p);
        assert_se(r < 0);
        assert_se(!p);

        /* Bind policy with node specification */
        r = parse_numa_mem_policy_from_string("interleave,0-1, 16 32", &p);
        assert_se(r == 0);
        assert_se(p);
        assert_se(p->type = NUMA_MEM_POLICY_TYPE_INTERLEAVE);
        assert_se(CPU_ISSET(0, p->nodemask) && CPU_ISSET(1, p->nodemask));
        assert_se(CPU_ISSET(16, p->nodemask) && CPU_ISSET(32, p->nodemask));
        free_numap(&p);

        /* Interleave policy with node specification */
        r = parse_numa_mem_policy_from_string("bind,1 3 5", &p);
        assert_se(r == 0);
        assert_se(p);
        assert_se(p->type = NUMA_MEM_POLICY_TYPE_BIND);
        assert_se(CPU_ISSET(1, p->nodemask) && CPU_ISSET(3, p->nodemask) && CPU_ISSET(5, p->nodemask));
        free_numap(&p);

        /* Prefered node policy */
        r = parse_numa_mem_policy_from_string("prefer,5", &p);
        assert_se(r == 0);
        assert_se(p->type == NUMA_MEM_POLICY_TYPE_PREFER);
        assert_se(CPU_ISSET(5, p->nodemask));
        free_numap(&p);

        /* Prefer invalid node */
        r = parse_numa_mem_policy_from_string("prefer,-5", &p);
        assert_se(r < 0);
        assert_se(!p);

        /* Prefer overflow node */
        r = parse_numa_mem_policy_from_string("prefer,100000000000000000000000000", &p);
        assert_se(r < 0);
        assert_se(!p);

        /* Prefered policy missing node*/
        r = parse_numa_mem_policy_from_string("prefer", &p);
        assert_se(r < 0);
        assert_se(!p);
        free_numap(&p);

        /* Prefered policy empty node spec*/
        r = parse_numa_mem_policy_from_string("prefer,", &p);
        assert_se(r < 0);
        assert_se(!p);
        free_numap(&p);

        /* Bind policy missing node specification */
        r = parse_numa_mem_policy_from_string("bind", &p);
        assert_se(r < 0);
        assert_se(!p);

        /* Bind policy with empty node specification */
        r = parse_numa_mem_policy_from_string("bind,", &p);
        assert_se(r < 0);
        assert_se(!p);

        /* Interleave without node specification */
        r = parse_numa_mem_policy_from_string("interleave", &p);
        assert_se(r < 0);
        assert_se(!p);
}


static void test_cpu_set_to_string_alloc(void) {
        const int NUM_CPUS = 16;
        int r;
        _cleanup_cpu_free_ cpu_set_t *set = CPU_ALLOC(NUM_CPUS);
        _cleanup_free_ char *s = NULL;

        CPU_ZERO_S(CPU_ALLOC_SIZE(NUM_CPUS), set);

        CPU_SET_S(0, CPU_ALLOC_SIZE(NUM_CPUS), set);
        CPU_SET_S(1, CPU_ALLOC_SIZE(NUM_CPUS), set);
        CPU_SET_S(4, CPU_ALLOC_SIZE(NUM_CPUS), set);
        CPU_SET_S(8, CPU_ALLOC_SIZE(NUM_CPUS), set);

        r = cpu_set_to_string_alloc(set, CPU_ALLOC_SIZE(NUM_CPUS), &s);
        assert_se(r == 0);
        assert_se(streq(s, "0 1 4 8"));
}

int main(int argc, char *argv[]) {
        test_parse_cpu_set();
        test_parse_numa_mem_policy();
        test_cpu_set_to_string_alloc();

        return 0;
}
