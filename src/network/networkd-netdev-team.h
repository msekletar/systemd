/*-*- Mode: C; c-basic-offset: 8; indent-tabs-mode: nil -*-*/

/***
    This file is part of systemd.

    Copyright 2015 Michal Sekletar <msekleta@redhat.com>

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

#pragma once

typedef struct Team Team;

#include "networkd-netdev.h"

typedef enum TeamMode {
        NETDEV_TEAM_MODE_BALANCE_RR,
        NETDEV_TEAM_MODE_BROADCAST,
        NETDEV_TEAM_MODE_RANDOM,
        _NETDEV_TEAM_MODE_MAX,
        _NETDEV_TEAM_MODE_INVALID = -1
} TeamMode;

struct Team {
        NetDev meta;

        TeamMode mode;
        char *teamd_bus_job;
        char *teamd_instance;
};

extern const NetDevVTable team_vtable;

const char* team_mode_to_string(TeamMode d) _const_;
TeamMode team_mode_from_string(const char *d) _pure_;

const char* team_runner_to_string(TeamMode d) _const_;
TeamMode team_runner_from_string(const char *d) _pure_;

int config_parse_team_mode(const char *unit, const char *filename, unsigned line, const char *section, unsigned section_line, const char *lvalue, int ltype, const char *rvalue, void *data, void *userdata);
