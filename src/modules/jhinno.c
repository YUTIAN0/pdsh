/*
 *****************************************************************************
 *  $Id$
 *****************************************************************************
 *  Copyright (C) 2001-2026.
 *  Written for pdsh integration with jhinno job scheduler.
 *
 *  This file is part of Pdsh, a parallel remote shell program.
 *  For details, see <http://www.llnl.gov/linux/pdsh/>.
 *
 *  Pdsh is free software; you can redistribute it and/or modify it under
 *  the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the License, or (at your option)
 *  any later version.
 *
 *  Pdsh is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with Pdsh; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
\*****************************************************************************/

#if HAVE_CONFIG_H
#  include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>

#include "src/common/hostlist.h"
#include "src/common/split.h"
#include "src/common/err.h"
#include "src/common/xmalloc.h"
#include "src/pdsh/mod.h"
#include "src/pdsh/opt.h"

#if STATIC_MODULES
#  define pdsh_module_info jhinno_module_info
#  define pdsh_module_priority jhinno_module_priority
#endif

int pdsh_module_priority = 10;

/*
 * Module operations
 */
static int mod_jhinno_init(void);
static int mod_jhinno_exit(void);
static int mod_jhinno_wcoll(opt_t *opt);
static int jhinno_process_opt(opt_t *, int opt, char *arg);

static List job_list = NULL;
static int include_unknown = 0;  /* Include UNKNOWN status nodes when -j all */

struct pdsh_module_operations jhinno_module_ops = {
    (ModInitF)       mod_jhinno_init,
    (ModExitF)       mod_jhinno_exit,
    (ModReadWcollF)  mod_jhinno_wcoll,
    (ModPostOpF)     NULL
};

/*
 * Module options
 */
struct pdsh_module_option jhinno_module_options[] = {
    { 'j', "jobid_or_group,...",
      "Run on nodes from jhinno job (number) or node group. "
      "Use special value 'all' to get all normal nodes.",
      DSH | PCP, (optFunc) jhinno_process_opt
    },
    { 0, "jhinno-include-unknown",
      "Include UNKNOWN status nodes when -j all",
      DSH | PCP, (optFunc) jhinno_process_opt
    },
    PDSH_OPT_TABLE_END
};

struct pdsh_module pdsh_module_info = {
    "misc",
    "jhinno",
    "Author",
    "Target nodes from jhinno job scheduler via jjobs/jhosts",
    DSH | PCP,
    &jhinno_module_ops,
    NULL,
    &jhinno_module_options[0],
};

static int mod_jhinno_init(void)
{
    return 0;
}

static int mod_jhinno_exit(void)
{
    if (job_list)
        list_destroy(job_list);
    return 0;
}

static int jhinno_process_opt(opt_t *pdsh_opts, int opt, char *arg)
{
    switch (opt) {
    case 'j':
        if (arg == NULL)
            return 0;
        job_list = list_split_append(job_list, ",", arg);
        break;
    case 0:  /* --jhinno-include-unknown */
        include_unknown = 1;
        break;
    default:
        break;
    }
    return 0;
}

/*
 * Check if string is a pure number (job id)
 */
static int _is_jobid_numeric(const char *s)
{
    if (s == NULL || *s == '\0')
        return 0;
    
    while (*s) {
        if (!isdigit((unsigned char)*s))
            return 0;
        s++;
    }
    return 1;
}

/*
 * Parse jjobs output: "64*ev-hpc-compute098:64*ev-hpc-compute164"
 * Extract node names, ignore the numeric prefix (CPU count)
 */
static hostlist_t _jhinno_wcoll_from_jobid(const char *jobid)
{
    FILE *fp = NULL;
    char cmd[1024];
    char line[8192];  /* Large buffer for long host lists */
    char line_copy[8192];  /* Copy for strtok */
    hostlist_t hl = NULL;
    int ret;
    
    (void)ret;  /* Suppress unused variable warning */
    
    if (jobid == NULL) {
        err("%p: jhinno: jobid is NULL\n");
        return NULL;
    }
    
    ret = snprintf(cmd, sizeof(cmd), "jjobs -o exec_host:4096 %s 2>/dev/null", jobid);
    if (ret < 0 || (size_t)ret >= sizeof(cmd)) {
        err("%p: jhinno: jobid too long\n");
        return NULL;
    }
    
    fp = popen(cmd, "r");
    if (fp == NULL) {
        err("%p: jhinno: Failed to execute jjobs command: %s\n", strerror(errno));
        return NULL;
    }
    
    /* Skip header line */
    if (fgets(line, sizeof(line), fp) == NULL) {
        err("%p: jhinno: No output from jjobs\n");
        pclose(fp);
        return NULL;
    }
    
    /* Read data line */
    if (fgets(line, sizeof(line), fp) != NULL) {
        /* Make a copy for strtok since it modifies the string */
        strncpy(line_copy, line, sizeof(line_copy) - 1);
        line_copy[sizeof(line_copy) - 1] = '\0';
        
        /* Remove newline */
        line_copy[strcspn(line_copy, "\n")] = '\0';
        
        /* Skip if still header */
        if (strstr(line_copy, "EXEC_HOST")) {
            if (fgets(line, sizeof(line), fp) == NULL) {
                err("%p: jhinno: No data line from jjobs\n");
                pclose(fp);
                return NULL;
            }
            strncpy(line_copy, line, sizeof(line_copy) - 1);
            line_copy[sizeof(line_copy) - 1] = '\0';
            line_copy[strcspn(line_copy, "\n")] = '\0';
        }
        
        /* Split by colon */
        char *token = strtok(line_copy, ":");
        while (token != NULL) {
            char hostname[256];
            char *sep;
            
            /* Skip whitespace */
            while (*token == ' ' || *token == '\t')
                token++;
            
            if (*token == '\0')
                continue;
            
            sep = strchr(token, '*');
            
            if (sep != NULL) {
                /* Format: "64*hostname" - extract hostname after '*' */
                size_t len = strlen(sep + 1);
                if (len == 0) {
                    token = strtok(NULL, ":");
                    continue;
                }
                len = (len < sizeof(hostname) - 1) ? len : sizeof(hostname) - 1;
                memcpy(hostname, sep + 1, len);
                hostname[len] = '\0';
            } else {
                /* No prefix, use as-is */
                size_t len = strlen(token);
                if (len == 0) {
                    token = strtok(NULL, ":");
                    continue;
                }
                len = (len < sizeof(hostname) - 1) ? len : sizeof(hostname) - 1;
                memcpy(hostname, token, len);
                hostname[len] = '\0';
            }
            
            /* Skip empty hostnames */
            if (strlen(hostname) > 0) {
                if (hl == NULL) {
                    hl = hostlist_create(hostname);
                    if (hl == NULL) {
                        err("%p: jhinno: Failed to create hostlist\n");
                        pclose(fp);
                        return NULL;
                    }
                } else {
                    ret = hostlist_push_host(hl, hostname);
                    if (ret < 0) {
                        err("%p: jhinno: Failed to add host to list\n");
                        /* Continue with other hosts */
                    }
                }
            }
            
            token = strtok(NULL, ":");
        }
    }
    
    if (fp) {
        if (pclose(fp) != 0) {
            err("%p: jhinno: jjobs command failed\n");
        }
    }
    
    if (hl)
        hostlist_uniq(hl);
    
    return hl;
}

/*
 * Parse jhosts output for specific node group
 * Format: skip header line, then read data rows, extract first column
 */
static hostlist_t _jhinno_wcoll_from_group(const char *nodegroup)
{
    FILE *fp = NULL;
    char cmd[1024];
    char line[8192];  /* Large buffer for long host lists */
    char line_copy[8192];  /* Copy for strtok */
    hostlist_t hl = NULL;
    int line_num = 0;
    int ret;
    
    if (nodegroup == NULL || nodegroup[0] == '\0') {
        err("%p: jhinno: nodegroup is NULL or empty\n");
        return NULL;
    }
    
    ret = snprintf(cmd, sizeof(cmd), "jhosts attrib -w %s 2>/dev/null", nodegroup);
    if (ret < 0 || (size_t)ret >= sizeof(cmd)) {
        err("%p: jhinno: nodegroup too long\n");
        return NULL;
    }
    
    fp = popen(cmd, "r");
    if (fp == NULL) {
        err("%p: jhinno: Failed to execute jhosts command: %s\n", strerror(errno));
        return NULL;
    }
    
    while (fgets(line, sizeof(line), fp) != NULL) {
        line_num++;
        
        /* Skip first line (header) */
        if (line_num == 1)
            continue;
        
        /* Make a copy for strtok since it modifies the string */
        strncpy(line_copy, line, sizeof(line_copy) - 1);
        line_copy[sizeof(line_copy) - 1] = '\0';
        
        /* Remove newline */
        line_copy[strcspn(line_copy, "\n")] = '\0';
        
        /* Skip empty lines */
        if (strlen(line_copy) == 0)
            continue;
        
        /* Extract hostname (first field) */
        char *hostname = strtok(line_copy, " \t");
        if (hostname == NULL || hostname[0] == '\0')
            continue;
        
        /* Skip whitespace */
        while (*hostname == ' ' || *hostname == '\t')
            hostname++;
        
        if (*hostname == '\0')
            continue;
        
        if (hl == NULL) {
            hl = hostlist_create(hostname);
            if (hl == NULL) {
                err("%p: jhinno: Failed to create hostlist\n");
                if (fp) pclose(fp);
                return NULL;
            }
        } else {
            ret = hostlist_push_host(hl, hostname);
            if (ret < 0) {
                err("%p: jhinno: Failed to add host to list\n");
                /* Continue with other hosts */
            }
        }
    }
    
    if (fp) {
        if (pclose(fp) != 0) {
            err("%p: jhinno: jhosts command failed\n");
        }
    }
    
    if (hl)
        hostlist_uniq(hl);
    
    return hl;
}

/*
 * Parse jhosts output for all nodes (jhosts attrib -w)
 * Format: skip header line, then read data rows
 * Extract first column (hostname) and second column (type)
 * Skip nodes with type == UNKNOWN unless include_unknown is set
 */
static hostlist_t _jhinno_wcoll_all(void)
{
    FILE *fp = NULL;
    char cmd[1024];
    char line[8192];  /* Large buffer for long host lists */
    char line_copy[8192];  /* Copy for strtok */
    hostlist_t hl = NULL;
    int line_num = 0;
    int ret;
    
    strncpy(cmd, "jhosts attrib -w 2>/dev/null", sizeof(cmd) - 1);
    cmd[sizeof(cmd) - 1] = '\0';
    
    fp = popen(cmd, "r");
    if (fp == NULL) {
        err("%p: jhinno: Failed to execute jhosts command: %s\n", strerror(errno));
        return NULL;
    }
    
    while (fgets(line, sizeof(line), fp) != NULL) {
        line_num++;
        
        /* Skip first line (header) */
        if (line_num == 1)
            continue;
        
        /* Make a copy for strtok since it modifies the string */
        strncpy(line_copy, line, sizeof(line_copy) - 1);
        line_copy[sizeof(line_copy) - 1] = '\0';
        
        /* Remove newline */
        line_copy[strcspn(line_copy, "\n")] = '\0';
        
        /* Skip empty lines */
        if (strlen(line_copy) == 0)
            continue;
        
        /* Extract hostname (first field) */
        char *hostname = strtok(line_copy, " \t");
        if (hostname == NULL || hostname[0] == '\0')
            continue;
        
        /* Skip whitespace */
        while (*hostname == ' ' || *hostname == '\t')
            hostname++;
        
        if (*hostname == '\0')
            continue;
        
        /* Extract type (second field) */
        char *type = strtok(NULL, " \t");
        if (type == NULL) {
            /* No type field, assume UNKNOWN */
            type = "";
        }
        
        /* Skip whitespace in type */
        while (*type == ' ' || *type == '\t')
            type++;
        
        /* Skip UNKNOWN nodes unless include_unknown is set */
        if (!include_unknown && strcmp(type, "UNKNOWN") == 0) {
            continue;
        }
        
        if (hl == NULL) {
            hl = hostlist_create(hostname);
            if (hl == NULL) {
                err("%p: jhinno: Failed to create hostlist\n");
                if (fp) pclose(fp);
                return NULL;
            }
        } else {
            ret = hostlist_push_host(hl, hostname);
            if (ret < 0) {
                err("%p: jhinno: Failed to add host to list\n");
                /* Continue with other hosts */
            }
        }
    }
    
    if (fp) {
        if (pclose(fp) != 0) {
            err("%p: jhinno: jhosts command failed\n");
        }
    }
    
    if (hl)
        hostlist_uniq(hl);
    
    return hl;
}

/*
 * Main function to get node list from various sources
 */
static int mod_jhinno_wcoll(opt_t *opt)
{
    hostlist_t hl = NULL;
    hostlist_t tmp_hl = NULL;
    ListIterator li = NULL;
    char *job = NULL;
    int ret;
    
    if (job_list && opt->wcoll) {
        errx("%p: do not specify -j with any other node selection option.\n");
    }
    
    /* 
     * Priority:
     * 1. -j options
     * 2. environment variable JOBS_JOBID
     */
    if (job_list == NULL) {
        char *env_var = getenv("JOBS_JOBID");
        if (env_var == NULL) {
            return 0;
        }
        
        /* Parse single jobid from env var */
        if (_is_jobid_numeric(env_var)) {
            hl = _jhinno_wcoll_from_jobid(env_var);
        } else if (strcmp(env_var, "all") == 0) {
            hl = _jhinno_wcoll_all();
        } else {
            hl = _jhinno_wcoll_from_group(env_var);
        }
    } else {
        /* Process list of jobs/groups */
        li = list_iterator_create(job_list);
        if (li == NULL) {
            err("%p: jhinno: Failed to create iterator\n");
            return -1;
        }
        
        while ((job = list_next(li))) {
            if (job == NULL || job[0] == '\0')
                continue;
            
            if (strcmp(job, "all") == 0) {
                tmp_hl = _jhinno_wcoll_all();
            } else if (_is_jobid_numeric(job)) {
                tmp_hl = _jhinno_wcoll_from_jobid(job);
            } else {
                tmp_hl = _jhinno_wcoll_from_group(job);
            }
            
            if (tmp_hl) {
                if (hl == NULL) {
                    hl = tmp_hl;
                } else {
                    ret = hostlist_push_list(hl, tmp_hl);
                    if (ret < 0) {
                        err("%p: jhinno: Failed to merge hostlists\n");
                    }
                    hostlist_destroy(tmp_hl);
                    tmp_hl = NULL;
                }
            }
        }
        list_iterator_destroy(li);
    }
    
    if (hl) {
        /* Final deduplication to handle overlapping nodes from multiple sources */
        hostlist_uniq(hl);
        opt->wcoll = hl;
    }
    
    (void)ret;  /* Suppress unused variable warning */
    
    return 0;
}

/*
 * vi: tabstop=4 shiftwidth=4 expandtab
 */
