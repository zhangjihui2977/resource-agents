#include "fd.h"
#include "ccs.h"

static int ccs_handle;

int setup_ccs(void)
{
	int i = 0, cd;

	while ((cd = ccs_connect()) < 0) {
		sleep(1);
		if (++i > 9 && !(i % 10))
			log_error("connect to ccs error %d, "
				  "check cluster status", cd);
	}

	ccs_handle = cd;
	return 0;
}

void close_ccs(void)
{
	ccs_disconnect(ccs_handle);
}

void read_ccs_name(char *path, char *name)
{
	char *str;
	int error;

	error = ccs_get(ccs_handle, path, &str);
	if (error || !str)
		return;

	strcpy(name, str);

	free(str);
}

void read_ccs_yesno(char *path, int *yes, int *no)
{
	char *str;
	int error;

	*yes = 0;
	*no = 0;

	error = ccs_get(ccs_handle, path, &str);
	if (error || !str)
		return;

	if (!strcmp(str, "yes"))
		*yes = 1;

	else if (!strcmp(str, "no"))
		*no = 1;

	free(str);
}

void read_ccs_int(char *path, int *config_val)
{
	char *str;
	int val;
	int error;

	error = ccs_get(ccs_handle, path, &str);
	if (error || !str)
		return;

	val = atoi(str);

	if (val < 0) {
		log_error("ignore invalid value %d for %s", val, path);
		return;
	}

	*config_val = val;
	log_debug("%s is %u", path, val);
	free(str);
}

#define OUR_NAME_PATH "/cluster/clusternodes/clusternode[@name=\"%s\"]/@name"
#define GROUPD_COMPAT_PATH "/cluster/group/@groupd_compat"
#define CLEAN_START_PATH "/cluster/fence_daemon/@clean_start"
#define POST_JOIN_DELAY_PATH "/cluster/fence_daemon/@post_join_delay"
#define POST_FAIL_DELAY_PATH "/cluster/fence_daemon/@post_fail_delay"
#define OVERRIDE_PATH_PATH "/cluster/fence_daemon/@override_path"
#define OVERRIDE_TIME_PATH "/cluster/fence_daemon/@override_time"

int read_ccs(struct fd *fd)
{
	char path[256];
	char *str;
	int error, i = 0, count = 0;

	/* Our own nodename must be in cluster.conf before we're allowed to
	   join the fence domain and then mount gfs; other nodes need this to
	   fence us. */

	str = NULL;
	memset(path, 0, 256);
	snprintf(path, 256, OUR_NAME_PATH, our_name);

	error = ccs_get(ccs_handle, path, &str);
	if (error || !str) {
		log_error("local cman node name \"%s\" not found in the "
			  "configuration", our_name);
		return error;
	}
	if (str)
		free(str);

	/* The comline config options are initially set to the defaults,
	   then options are read from the command line to override the
	   defaults, for options not set on command line, we look for
	   values set in cluster.conf. */

	if (!comline.groupd_compat_opt)
		read_ccs_int(GROUPD_COMPAT_PATH, &comline.groupd_compat);
	if (!comline.clean_start_opt)
		read_ccs_int(CLEAN_START_PATH, &comline.clean_start);
	if (!comline.post_join_delay_opt)
		read_ccs_int(POST_JOIN_DELAY_PATH, &comline.post_join_delay);
	if (!comline.post_fail_delay_opt)
		read_ccs_int(POST_FAIL_DELAY_PATH, &comline.post_fail_delay);
	if (!comline.override_time_opt)
		read_ccs_int(OVERRIDE_TIME_PATH, &comline.override_time);

	if (!comline.override_path_opt) {
		str = NULL;
		memset(path, 0, 256);
		sprintf(path, OVERRIDE_PATH_PATH);

		error = ccs_get(ccs_handle, path, &str);
		if (!error && str) {
			free(comline.override_path);
			comline.override_path = strdup(str);
		}
		if (str)
			free(str);
	}

	if (comline.clean_start) {
		log_debug("clean start, skipping initial nodes");
		goto out;
	}

	for (i = 1; ; i++) {
		str = NULL;
		memset(path, 0, 256);
		sprintf(path, "/cluster/clusternodes/clusternode[%d]/@nodeid", i);

		error = ccs_get(ccs_handle, path, &str);
		if (error || !str)
			break;

		add_complete_node(fd, atoi(str));
		free(str);
		count++;
	}

	log_debug("added %d nodes from ccs", count);
 out:
	return 0;
}

