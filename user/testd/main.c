#include <asm-generic/errno.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <limits.h>
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/eventfd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#include "proxy_dev.h"
#include "proxy_ioctl.h"
#include "shared_mem.h"

extern int ecc_sw_hamming_calculate(const unsigned char *buf,
    unsigned int step_size, unsigned char *code, bool sm_order);
extern int ecc_sw_hamming_correct(unsigned char *buf, unsigned char *read_ecc,
    unsigned char *calc_ecc, unsigned int step_size, bool sm_order);

static int s_errno_rc;

struct program_params {
	bool verbose;
	bool timeout;
	bool exit_on_error;
	unsigned int nack_reads_errno;
	unsigned int nack_writes_errno;
	bool inject_read_errors;
	bool inject_write_errors;
	char *path;
};

static struct program_params prog_params;
static atomic_bool stop = false;
static struct proxy_device_queue_state queues[PROXY_MAX_QUEUES_COUNT];
static struct proxy_device_state state;

static struct option long_options[] = {{"help", no_argument, 0, 'h'},
    {"verbose", no_argument, 0, 'v'}, {"timeout", no_argument, 0, 't'},
    {"exit-on-error", no_argument, 0, 'E'},
    {"nack-writes", required_argument, 0, 'N'},
    {"nack-reads", required_argument, 0, 'W'},
    {"inject-read-errors", no_argument, 0, 'I'},
    {"inject-write-errors", no_argument, 0, 'w'}, {0, 0, 0, 0}};

static void sigint_handler(int sig)
{
	(void)sig;
	atomic_store(&stop, true);
}

static void print_help(const char *prog)
{
	printf("Usage: %s [options] <file>\n\n", prog);

	printf("Options:\n");
	printf("  -h, --help                     Show help.\n");
	printf("  -v, --verbose                  Enable verbose output.\n");
	printf("  -t, --timeout                  Don't answer with ioctls at "
	       "all.\n");
	printf("  -E, --exit-on-error            Exit on first error.\n");
	printf("  -N, --nack-writes <errno>      Answer with NACKs with a "
	       "specific errno for write requests.\n");
	printf("  -N, --nack-reads <errno>      Answer with NACKs with a "
	       "specific errno for read requests.\n");
}

static void fatal_stop_callback(pid_t tid, int err)
{
	fprintf(stderr, "Fatal error, TID %d, error: %d, %s\n", tid, err,
	    strerror(err));
	atomic_store(&stop, true);
}

static bool verify_has_enough_oob_storage_for_hamming_ecc(size_t data_len)
{
	size_t ecc_bytes_count = (state.mtd_info.flash_oob_size - 2);
	return (data_len / 256) <= (ecc_bytes_count / 3);
}

static transform_answer_t hamming_write_process(const struct shm_slot_hdr *hdr,
    const u8 *entire_page_buf, struct transform_context_results *context_res)
{
	int ret;
	size_t ecc_step_idx;
	size_t oob_offset =
	    state.mtd_info.flash_page_size - state.mtd_info.flash_oob_size;

	// Get to the OOB region + 2 bytes offset of bad block marker.
	u8 *eccbuf = (u8 *)entire_page_buf + (oob_offset) + 2;
	u8 *databuf = (u8 *)entire_page_buf;

	if (!verify_has_enough_oob_storage_for_hamming_ecc(hdr->datalen)) {
		context_res->errno_rc = -EOPNOTSUPP;
		return ANSWER_NACK;
	}

	// Mark two bytes for bad block marker (0xFF)
	// FIXME: Do we actually want to mark it on **EVERY** page?
	databuf[oob_offset] = 0xFF;
	databuf[oob_offset + 1] = 0xFF;

	unsigned char ecccalc[3];
	for (ecc_step_idx = 0; ecc_step_idx < (hdr->datalen / 256);
	    ecc_step_idx++) {
		ret = ecc_sw_hamming_calculate(databuf, 256, ecccalc, false);
		if (ret < 0) {
			context_res->errno_rc = ret;
			return ANSWER_NACK;
		}
		memcpy(eccbuf, ecccalc, 3);
		eccbuf += 3;
		databuf += 256;
	}

	context_res->data_retlen = hdr->datalen;
	context_res->oob_retlen = hdr->ooblen;
	context_res->errno_rc = 0;
	return ANSWER_ACK;
}

static transform_answer_t hamming_read_process(const struct shm_slot_hdr *hdr,
    const u8 *entire_page_buf, struct transform_context_results *context_res)
{
	int stat;
	size_t ecc_step_idx;
	size_t oob_offset =
	    state.mtd_info.flash_page_size - state.mtd_info.flash_oob_size;

	// Get to the OOB region + 2 bytes offset of bad block marker.
	u8 *eccbuf = (u8 *)entire_page_buf + (oob_offset) + 2;
	u8 *databuf = (u8 *)entire_page_buf;

	if (!verify_has_enough_oob_storage_for_hamming_ecc(hdr->datalen))
		return ANSWER_NACK;

	unsigned char ecccalc[3];
	for (ecc_step_idx = 0; ecc_step_idx < (hdr->datalen / 256);
	    ecc_step_idx++) {
		stat = ecc_sw_hamming_calculate(databuf, 256, ecccalc, false);
		if (stat < 0) {
			context_res->errno_rc = stat;
			return ANSWER_NACK;
		}
		stat = ecc_sw_hamming_correct(
		    databuf, eccbuf, ecccalc, 256, false);
		if (stat < 0) {
			if (prog_params.verbose)
				fprintf(stderr, "%s: uncorrectable ECC error\n",
				    __func__);
			context_res->errno_rc = stat;
			return ANSWER_NACK;
		}
	}

	context_res->data_retlen = hdr->datalen;
	context_res->oob_retlen = hdr->ooblen;
	context_res->errno_rc = 0;
	return ANSWER_ACK;
}

static transform_answer_t dummy_nack_process(const struct shm_slot_hdr *hdr,
    const u8 *entire_page_buf, struct transform_context_results *context_res)
{
	(void)hdr;
	(void)entire_page_buf;

	context_res->errno_rc = s_errno_rc;

	if (prog_params.timeout) {
		// FIXME: 3 seconds feel arbitrary, but should work for now...
		struct timespec ts;
		ts.tv_sec = 3;
		ts.tv_nsec = 0;
		nanosleep(&ts, NULL);
	}

	return ANSWER_NACK;
}

static int create_queues(struct proxy_device_state *state)
{
	int ret;

	struct proxy_queue_func_ops func_ops = {
	    .fatal_stop = NULL,
	    .write = NULL,
	    .read = NULL,
	};

	func_ops.write = hamming_write_process;
	func_ops.read = hamming_read_process;

	if (prog_params.nack_writes_errno != 0) {
		func_ops.write = dummy_nack_process;
	}

	if (prog_params.nack_reads_errno != 0) {
		func_ops.read = dummy_nack_process;
	}

	if (prog_params.timeout) {
		func_ops.read = dummy_nack_process;
		func_ops.write = dummy_nack_process;
	}

	if (prog_params.exit_on_error) {
		func_ops.fatal_stop = fatal_stop_callback;
	}

	ret = create_proxy_dev_queue_state(state, &queues[0], 0, &func_ops);
	if (ret < 0) {
		goto exit;
	}

	ret = create_proxy_dev_queue_state(state, &queues[1], 1, &func_ops);
	if (ret < 0) {
		goto destroy_first_queue;
	}

	return 0;

destroy_first_queue:
	destroy_proxy_dev_queue_state(&queues[0]);
exit:
	return ret;
}

static int parse_uint(const char *s, unsigned int *out)
{
	char *end;
	unsigned long val;

	errno = 0;
	val = strtoul(s, &end, 10);

	if (errno == ERANGE || val > UINT_MAX)
		return -1;

	if (end == s || *end != '\0')
		return -1;

	*out = (unsigned int)val;
	return 0;
}

void print_params()
{
	if (!prog_params.verbose)
		return;

	printf("Verbose enabled\n");

	printf("File: %s\n", prog_params.path);

	if (prog_params.nack_reads_errno != 0)
		printf("NACK read errno: %d (%s)\n",
		    prog_params.nack_reads_errno,
		    strerror(prog_params.nack_reads_errno));

	if (prog_params.nack_writes_errno != 0)
		printf("NACK write errno: %d (%s)\n",
		    prog_params.nack_writes_errno,
		    strerror(prog_params.nack_writes_errno));

	if (prog_params.timeout)
		printf("IOCTL responses disabled\n");

	if (prog_params.exit_on_error)
		printf("Exit on first error enabled\n");
}

int handle_params(int argc, char **argv)
{
	int opt;
	int failed_parsing_nack_reads_errno = 0;
	int failed_parsing_nack_writes_errno = 0;

	while ((opt = getopt_long(
		    argc, argv, "hvtDEN:Iw", long_options, NULL)) != -1) {
		switch (opt) {
		case 'h':
			print_help(argv[0]);
			return EXIT_SUCCESS;

		case 'v':
			prog_params.verbose = 1;
			break;

		case 't':
			prog_params.timeout = 1;
			break;

		case 'E':
			prog_params.exit_on_error = 1;
			break;

		case 'N':
			failed_parsing_nack_reads_errno =
			    parse_uint(optarg, &prog_params.nack_reads_errno);
			break;

		case 'W':
			failed_parsing_nack_writes_errno =
			    parse_uint(optarg, &prog_params.nack_writes_errno);
			break;

		default:
			print_help(argv[0]);
			return 1;
		}
	}

	/* Remaining argument is the file */
	if (optind >= argc) {
		fprintf(stderr, "Error: missing file argument\n\n");
		print_help(argv[0]);
		return 1;
	}

	prog_params.path = argv[optind];

	if (failed_parsing_nack_reads_errno < 0) {
		fprintf(stderr, "Failed parsing NACK reads errno");
		return 1;
	}

	if (failed_parsing_nack_writes_errno < 0) {
		fprintf(stderr, "Failed parsing NACK reads errno");
		return 1;
	}

	return 0;
}

int main(int argc, char **argv)
{
	int ret;
	struct timespec ts;

	struct sigaction sa = {
	    .sa_handler = sigint_handler,
	};

	memset(&prog_params, 0, sizeof(prog_params));

	if (handle_params(argc, argv) != 0)
		return EXIT_FAILURE;

	print_params();

	sigemptyset(&sa.sa_mask);
	sa.sa_flags = 0;
	ret = sigaction(SIGINT, &sa, NULL);

	if (ret < 0) {
		perror("sigaction");
		goto exit;
	}

	ret = open_proxy_device_state(prog_params.path, &state);
	if (ret < 0) {
		goto exit;
	}

	ret = create_queues(&state);
	if (ret < 0)
		goto destroy_proxy_device_state;

	ts.tv_sec = 0;
	ts.tv_nsec = 100 * 1000 * 1000; // 100 ms
	while (!atomic_load(&stop)) {
		nanosleep(&ts, NULL);
	}

	destroy_proxy_dev_queue_state(&queues[0]);
	destroy_proxy_dev_queue_state(&queues[1]);

destroy_proxy_device_state:
	close_proxy_device(&state);
exit:
	return ret == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
