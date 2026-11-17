/* race-test.c - demonstrates the relative-path limitation documented in the
 * directory-flush patch: LMDB resolves the data file, the lock file and (with
 * the patch) the directory at different moments, so a thread calling
 * SetCurrentDirectoryW() in between can make them land in different
 * directories.  This program does not try to prove the bug away -- it counts
 * how often the environment lands in the "wrong" directory.
 *
 * usage: race-test <dirA> <dirB> <iterations>
 * Prints: iters=... open_fail=... landedA=... landedB=... other=...
 */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lmdb.h"

static volatile LONG g_stop;
static WCHAR g_a[MAX_PATH], g_b[MAX_PATH];

static DWORD WINAPI flipper(LPVOID unused)
{
	int i = 0;
	(void)unused;
	while (!InterlockedCompareExchange(&g_stop, 0, 0)) {
		SetCurrentDirectoryW((i++ & 1) ? g_a : g_b);
	}
	return 0;
}

static void wide(const char *src, WCHAR *dst)
{
	MultiByteToWideChar(CP_UTF8, 0, src, -1, dst, MAX_PATH);
}

static int file_exists(const char *path)
{
	DWORD a = GetFileAttributesA(path);
	return a != INVALID_FILE_ATTRIBUTES;
}

static void cleanup(const char *dir)
{
	char p[MAX_PATH];
	const char *names[] = { "\\relenv\\data.mdb", "\\relenv\\lock.mdb" };
	int i;
	for (i = 0; i < 2; i++) {
		snprintf(p, sizeof(p), "%s%s", dir, names[i]);
		DeleteFileA(p);
	}
	/* keep the directory itself: LMDB requires the env directory to exist */
}

int main(int argc, char **argv)
{
	int iters, i, fail = 0, in_a = 0, in_b = 0, other = 0;
	char a_data[MAX_PATH], b_data[MAX_PATH], a_rel[MAX_PATH], b_rel[MAX_PATH];
	HANDLE th;

	if (argc < 4) {
		fprintf(stderr, "usage: %s <dirA> <dirB> <iterations>\n", argv[0]);
		return 2;
	}
	iters = atoi(argv[3]);
	wide(argv[1], g_a);
	wide(argv[2], g_b);
	snprintf(a_data, sizeof(a_data), "%s\\relenv\\data.mdb", argv[1]);
	snprintf(b_data, sizeof(b_data), "%s\\relenv\\data.mdb", argv[2]);
	snprintf(a_rel, sizeof(a_rel), "%s\\relenv", argv[1]);
	snprintf(b_rel, sizeof(b_rel), "%s\\relenv", argv[2]);
	CreateDirectoryA(argv[1], NULL);
	CreateDirectoryA(argv[2], NULL);
	CreateDirectoryA(a_rel, NULL);
	CreateDirectoryA(b_rel, NULL);

	th = CreateThread(NULL, 0, flipper, NULL, 0, NULL);
	if (!th) {
		fprintf(stderr, "CreateThread failed\n");
		return 3;
	}
	for (i = 0; i < iters; i++) {
		MDB_env *env;
		int rc;
		cleanup(argv[1]);
		cleanup(argv[2]);
		SetCurrentDirectoryW(g_a);
		if (mdb_env_create(&env) != MDB_SUCCESS) {
			fail++;
			continue;
		}
		mdb_env_set_mapsize(env, (mdb_size_t)1 << 20);
		rc = mdb_env_open(env, "relenv", 0, 0664);
		if (rc != MDB_SUCCESS) {
			if (!fail)
				printf("first failure: rc=%d %s\n", rc,
				       mdb_strerror(rc));
			fail++;
			mdb_env_close(env);
			continue;
		}
		mdb_env_close(env);
		if (file_exists(a_data))
			in_a++;
		else if (file_exists(b_data))
			in_b++;
		else
			other++;
	}
	InterlockedExchange(&g_stop, 1);
	WaitForSingleObject(th, 5000);
	CloseHandle(th);
	cleanup(argv[1]);
	cleanup(argv[2]);
	printf("iters=%d open_fail=%d landedA=%d landedB=%d other=%d\n",
	       iters, fail, in_a, in_b, other);
	return 0;
}
