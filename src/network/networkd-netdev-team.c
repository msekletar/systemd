/*-*- Mode: C; c-basic-offset: 8; indent-tabs-mode: nil -*-*/

/***
    This file is part of systemd.

    Copyright 2015 Michal Sekletar

    systemd is free software; you can redistribute it and/or modify it
    under the terms of the GNU Lesser General Public License as published by
    the Free Software Foundation; either version 2.1 of the License, or
    (at your option) any later version.

    systemd is distributed in the hope that it will be useful, but
    WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
    Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public License
    along with systemd; If not, see <http://www.gnu.org/licenses/>.
***/

#include "conf-parser.h"
#include "sd-netlink.h"
#include "networkd-netdev-team.h"
#include "missing.h"
#include "sd-bus.h"
#include "bus-util.h"


static const char* const team_mode_table[_NETDEV_TEAM_MODE_MAX] = {
        [NETDEV_TEAM_MODE_BALANCE_RR] = "balance-rr",
        [NETDEV_TEAM_MODE_BROADCAST] = "broadcast",
        [NETDEV_TEAM_MODE_RANDOM] = "random",
};

DEFINE_STRING_TABLE_LOOKUP(team_mode, TeamMode);
DEFINE_CONFIG_PARSE_ENUM(config_parse_team_mode, team_mode, TeamMode, "Failed to parse team mode");

static const char* const team_runner_table[_NETDEV_TEAM_MODE_MAX] = {
        [NETDEV_TEAM_MODE_BALANCE_RR] = "roundrobin",
        [NETDEV_TEAM_MODE_BROADCAST] = "broadcast",
        [NETDEV_TEAM_MODE_RANDOM] = "random",
};

DEFINE_STRING_TABLE_LOOKUP(team_runner, TeamMode);

static int netdev_team_fill_message_create(NetDev *netdev, Link *link, sd_netlink_message *m) {
        Team *t = TEAM(netdev);

        assert(netdev);
        assert(!link);
        assert(t);
        assert(m);

        return 0;
}

static void team_init(NetDev *netdev) {
        Team *team = TEAM(netdev);

        assert(netdev);
        assert(team);

        team->mode = NETDEV_TEAM_MODE_BALANCE_RR;
}

static void team_done(NetDev *netdev) {
        Team *team = TEAM(netdev);

        assert(netdev);
        assert(team);

        free(team->teamd_bus_job);
        team->teamd_bus_job = NULL;

        if (team->teamd_instance) {
                /* Try to stop teamd instance unit */
                sd_bus_call_method(netdev->manager->bus,
                                   "org.freedesktop.systemd1",
                                   "/org/freedesktop/systemd1",
                                   "org.freedesktop.systemd1.Manager",
                                   "StopUnit",
                                   NULL,
                                   NULL,
                                   "ss", team->teamd_instance, "fail");

                free(team->teamd_instance);
                team->teamd_instance = NULL;
        }
}

static int team_write_config(Team *t) {
        _cleanup_fclose_ FILE *f = NULL;
        char *p;

        assert(t);

        p = strjoina("/run/teamd/", NETDEV(t)->ifname, ".conf");

        f = fopen(p, "we");
        if (!f)
                return -errno;

        fprintf(f, "{\"runner\": {\"name\":\"%s\"}}", team_runner_to_string(t->mode));

        return 0;
}

static int teamd_bus_job_handler(sd_bus_message *message, void *userdata, sd_bus_error *ret_error) {
        _cleanup_netdev_unref_ NetDev *netdev = userdata;
        Manager *m = netdev->manager;
        int r;
        char *path;
        _cleanup_free_ char *teamd_bus_job = NULL;

        assert(m);
        assert(netdev);

        r = sd_bus_message_read(message, "o", &path);
        if (r < 0)
                return bus_log_parse_error(r);

        teamd_bus_job = strdup(path);
        if (!teamd_bus_job)
                return -ENOMEM;

        r = hashmap_ensure_allocated(&m->netdev_by_job_path, &string_hash_ops);
        if (r < 0)
                return r;

        r = hashmap_put(m->netdev_by_job_path, teamd_bus_job, netdev_ref(netdev));
        if (r < 0)
                return r;

        TEAM(netdev)->teamd_bus_job = teamd_bus_job;
        teamd_bus_job = NULL;

        return 0;
}

static int netdev_team_post_create(NetDev *netdev, Link *link, sd_netlink_message *m) {
        Team *t = TEAM(netdev);
        int r;
        char *unit_name;

        assert(netdev);
        assert(!link);
        assert(t);
        assert(m);

        r = team_write_config(t);
        if (r < 0) {
                log_error_errno(r, "Failed to write configuration for team device: %m");
                return -errno;
        }

        if (!netdev->manager->bus) {
                /* TODO: Remove this once we have kdbus, everywhere */
                log_netdev_info(netdev, "Not connected to system bus, can't spawn teamd instance");
                return 0;
        }

        unit_name = strjoina("teamd@", netdev->ifname, ".service", NULL);

        r = sd_bus_call_method_async(netdev->manager->bus,
                                     NULL,
                                     "org.freedesktop.systemd1",
                                     "/org/freedesktop/systemd1",
                                     "org.freedesktop.systemd1.Manager",
                                     "StartUnit",
                                     teamd_bus_job_handler,
                                     netdev,
                                     "ss",
                                     unit_name,
                                     "replace");
        if (r < 0)
                return log_netdev_error_errno(netdev, r, "Failed to spawn teamd instance: %m");

        return 0;
}

const NetDevVTable team_vtable = {
        .object_size = sizeof(Team),
        .init = team_init,
        .done = team_done,
        .sections = "Match\0NetDev\0Team\0",
        .fill_message_create = netdev_team_fill_message_create,
        .post_create = netdev_team_post_create,
        .create_type = NETDEV_CREATE_MASTER,
};
