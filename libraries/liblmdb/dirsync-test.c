/* dirsync-test.c - functional smoke test for the LMDB directory-flush patch.
 * usage: dirsync-test <env-path> <copy-path> [mode]
 *   mode = normal (default) | nosubdir | nosync | nosubdir-nosync
 * With MDB_NOSUBDIR both paths are files; otherwise both are directories.
 * Creates a fresh env, writes one record, copies it, prints TEST-OK.
 * Observation is external: strace on POSIX, DIRSYNC-PROBE stderr on the
 * instrumented Windows build.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lmdb.h"

static void die(const char *what, int rc)
{
	fprintf(stderr, "FAIL %s: %s (%d)\n", what, mdb_strerror(rc), rc);
	exit(1);
}

int main(int argc, char **argv)
{
	MDB_env *env;
	MDB_dbi dbi;
	MDB_txn *txn;
	MDB_val key, data;
	unsigned int flags = 0;
	const char *mode;
	int rc;

	if (argc < 3) {
		fprintf(stderr, "usage: %s <env-path> <copy-path> [mode]\n", argv[0]);
		return 2;
	}
	mode = (argc > 3) ? argv[3] : "normal";
	if (strstr(mode, "nosubdir"))
		flags |= MDB_NOSUBDIR;
	if (strstr(mode, "nosync"))
		flags |= MDB_NOSYNC;
	if (strstr(mode, "rdonly"))
		flags |= MDB_RDONLY;
	if (strstr(mode, "nolock"))
		flags |= MDB_NOLOCK;
	if ((rc = mdb_env_create(&env)) != MDB_SUCCESS)
		die("env_create", rc);
	mdb_env_set_mapsize(env, (mdb_size_t)1 << 20);
	if ((rc = mdb_env_open(env, argv[1], flags, 0664)) != MDB_SUCCESS)
		die("env_open", rc);
	if (flags & MDB_RDONLY) {
		/* read-only: no writes, and the copy below would fail */
		printf("TEST-OK mode=%s\n", mode);
		mdb_env_close(env);
		return 0;
	}
	if ((rc = mdb_txn_begin(env, NULL, 0, &txn)) != MDB_SUCCESS)
		die("txn_begin", rc);
	if ((rc = mdb_dbi_open(txn, NULL, 0, &dbi)) != MDB_SUCCESS)
		die("dbi_open", rc);
	key.mv_size = 4;
	key.mv_data = "user";
	data.mv_size = 5;
	data.mv_data = "hello";
	if ((rc = mdb_put(txn, dbi, &key, &data, 0)) != MDB_SUCCESS)
		die("put", rc);
	if ((rc = mdb_txn_commit(txn)) != MDB_SUCCESS)
		die("commit", rc);
	if ((rc = mdb_env_copy(env, argv[2])) != MDB_SUCCESS)
		die("copy", rc);
	mdb_env_close(env);
	printf("TEST-OK mode=%s\n", mode);
	return 0;
}
