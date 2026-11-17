/* e25_lmdb_bench.c - cost benchmark for the LMDB directory-flush patch.
 *
 * Phase A: create N fresh environments (each in its own new directory, so
 *          data.mdb and lock.mdb are newly created and mdb_env_open()'s
 *          mdb_mdirsync() runs every time).  This is where the patch costs
 *          anything: the pristine library performs no directory flush, the
 *          dir variant performs one FlushFileBuffers, the dir+root variant
 *          adds the volume-root flush.
 * Phase B: N transactions inside one pre-created environment.  The patch
 *          does not touch the commit path, so all three variants should
 *          time the same here (a discriminating control: if dir+root is
 *          slower in phase B, something is flushing where it should not).
 *
 * usage: e25_lmdb_bench <work-root> <env-iters> <txn-iters> [label]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "lmdb.h"

#ifdef _WIN32
#include <windows.h>
static int mkdir1(const char *p) { return CreateDirectoryA(p, NULL) ? 0 : -1; }
static double now_ms(void) {
	LARGE_INTEGER f, t;
	QueryPerformanceFrequency(&f);
	QueryPerformanceCounter(&t);
	return (double)t.QuadPart * 1000.0 / (double)f.QuadPart;
}
#else
#include <sys/stat.h>
#include <sys/time.h>
static int mkdir1(const char *p) { return mkdir(p, 0777); }
static double now_ms(void) {
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return (double)tv.tv_sec * 1000.0 + (double)tv.tv_usec / 1000.0;
}
#endif

static void die(const char *what, int rc)
{
	fprintf(stderr, "FAIL %s: %s (%d)\n", what, mdb_strerror(rc), rc);
	exit(1);
}

int main(int argc, char **argv)
{
	const char *root, *label;
	char path[1024], sub[1024];
	MDB_env *env;
	MDB_dbi dbi;
	MDB_txn *txn;
	MDB_val key, data;
	double t0, t1, t2, t3;
	int i, nEnv, nTxn, rc;

	if (argc < 4) {
		fprintf(stderr,
			"usage: e25_lmdb_bench <work-root> <env-iters> <txn-iters> [label]\n");
		return 2;
	}
	root = argv[1];
	nEnv = atoi(argv[2]);
	nTxn = atoi(argv[3]);
	label = argc > 4 ? argv[4] : "x";

	key.mv_size = 4;
	key.mv_data = "user";
	data.mv_size = 5;
	data.mv_data = "hello";

	/* Phase A: fresh env per iteration; mkdir the iteration dir itself
	 * so the env directory is newly created too. */
	t0 = now_ms();
	for (i = 0; i < nEnv; i++) {
		snprintf(path, sizeof path, "%s/e%05d", root, i);
		mkdir1(path);
		if ((rc = mdb_env_create(&env)) != MDB_SUCCESS)
			die("env_create", rc);
		mdb_env_set_mapsize(env, (mdb_size_t)1 << 20);
		if ((rc = mdb_env_open(env, path, 0, 0664)) != MDB_SUCCESS)
			die("env_open", rc);
		if ((rc = mdb_txn_begin(env, NULL, 0, &txn)) != MDB_SUCCESS)
			die("txn_begin", rc);
		if ((rc = mdb_dbi_open(txn, NULL, 0, &dbi)) != MDB_SUCCESS)
			die("dbi_open", rc);
		if ((rc = mdb_put(txn, dbi, &key, &data, 0)) != MDB_SUCCESS)
			die("put", rc);
		if ((rc = mdb_txn_commit(txn)) != MDB_SUCCESS)
			die("txn_commit", rc);
		mdb_env_close(env);
	}
	t1 = now_ms();

	/* Phase B: one env, N commits; per-commit cost must not differ. */
	snprintf(sub, sizeof sub, "%s/single", root);
	mkdir1(sub);
	if ((rc = mdb_env_create(&env)) != MDB_SUCCESS)
		die("env_create", rc);
	mdb_env_set_mapsize(env, (mdb_size_t)64 << 20);
	if ((rc = mdb_env_open(env, sub, 0, 0664)) != MDB_SUCCESS)
		die("env_open", rc);
	if ((rc = mdb_txn_begin(env, NULL, 0, &txn)) != MDB_SUCCESS)
		die("txn_begin", rc);
	if ((rc = mdb_dbi_open(txn, NULL, 0, &dbi)) != MDB_SUCCESS)
		die("dbi_open", rc);
	if ((rc = mdb_txn_commit(txn)) != MDB_SUCCESS)
		die("txn_commit", rc);

	t2 = now_ms();
	for (i = 0; i < nTxn; i++) {
		snprintf((char *)key.mv_data, key.mv_size, "k%04d", i & 9999);
		if ((rc = mdb_txn_begin(env, NULL, 0, &txn)) != MDB_SUCCESS)
			die("txn_begin", rc);
		if ((rc = mdb_put(txn, dbi, &key, &data, 0)) != MDB_SUCCESS)
			die("put", rc);
		if ((rc = mdb_txn_commit(txn)) != MDB_SUCCESS)
			die("txn_commit", rc);
	}
	t3 = now_ms();

	printf("%s: phaseA %.3f ms/env | phaseB %.4f ms/txn | envs=%d txns=%d\n",
		label, (t1 - t0) / (nEnv ? nEnv : 1), (t3 - t2) / (nTxn ? nTxn : 1),
		nEnv, nTxn);
	mdb_env_close(env);
	return 0;
}
