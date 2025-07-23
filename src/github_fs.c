#include <errno.h>
#include <fuse.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include <curl/curl.h>
#include <json-c/json.h>

typedef struct {
	const char *value;
	size_t len;
} sv_t;

typedef struct {
	sv_t user;
	sv_t repo;
	sv_t branch;
	sv_t path;
} fs_github_path;

int fs_github_path_parse_from_path(fs_github_path *fgp, const char *path)
{
	fgp->user.value = NULL;
	fgp->repo.value = NULL;
	fgp->branch.value = NULL;
	fgp->path.value = NULL;

	const char *current = path + 1;
	const char *next;
	size_t index = 0;

	while (*current != '\0') {
		next = current;
		while (*next != '\0' && *next != '/') {
			++next;
		}

		switch (index) {
			case 0: {
				fgp->user.value = current;
				fgp->user.len = next - current;
			} break;
			case 1: {
				fgp->repo.value = current;
				fgp->repo.len = next - current;
			} break;
			case 2: {
				fgp->branch.value = current;
				fgp->branch.len = next - current;
			} break;
			case 3: {
				fgp->user.value = current;
				fgp->user.len = strlen(current);

				return 0;
			} break;
		}

		index++;

		current = (*next == '/') ? next + 1 : next;
	}

	return 0;
}

json_object *github_api_users_repos(sv_t *user)
{
	FILE *file = tmpfile();

	CURL *curl = curl_easy_init();

	const char *fs_github_users_repos_url_p1 = "https://api.github.com/users/";
	const char *fs_github_users_repos_url_p2 = "/repos";

	// TODO check for errors
	char *fs_github_users_repos_url = calloc(user->len + strlen(fs_github_users_repos_url_p1) + strlen(fs_github_users_repos_url_p2) + 1, sizeof(char));

	strcpy(fs_github_users_repos_url, fs_github_users_repos_url_p1);
	strncat(fs_github_users_repos_url, user->value, user->len);
	strcat(fs_github_users_repos_url, fs_github_users_repos_url_p2);

	curl_easy_setopt(curl, CURLOPT_URL, fs_github_users_repos_url);

	curl_easy_setopt(curl, CURLOPT_USERAGENT, "github_fs");
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, file);

	curl_easy_perform(curl);

	rewind(file);

	char *json_str = NULL;
	const size_t block = 4096;
	size_t nread_total = 0;

	do {
		json_str = (char *)realloc(json_str, nread_total + block);

		nread_total += fread(json_str + nread_total, sizeof(char), block, file);
	} while (feof(file) == 0);

	json_object *root = json_tokener_parse(json_str);

	free(json_str);

	return root;
}

static int fs_readdir(const char *path, void *data, fuse_fill_dir_t filler, off_t off, struct fuse_file_info *ffi)
{
	(void)off;
	(void)ffi;

	fs_github_path fgp;
	int fgp_parse_result;

	if ((fgp_parse_result = fs_github_path_parse_from_path(&fgp, path)) != 0) {
		return fgp_parse_result;
	}

	/* USERS */
	if (fgp.user.value == NULL) {
		// curl https://api.github.com/search/users?q=jd297

		filler(data, ".", NULL, 0);
		filler(data, "..", NULL, 0);
		filler(data, "jd297", NULL, 0); // TODO HARD

		return 0;
	}

	/* REPOSITORIES */
	if (fgp.user.value != NULL && fgp.repo.value == NULL) {
		if (off != 0) { // TODO: research this! but the hack for now works! :)
			return 0;
		}

		json_object *root = github_api_users_repos(&fgp.user);

		size_t array_length = json_object_array_length(root);

		for (size_t i = 0; i < array_length; i++) {
			struct json_object* json_obj = json_object_array_get_idx(root, i);

			struct json_object* name_obj;

			if (json_object_object_get_ex(json_obj, "name", &name_obj)) {
				filler(data, json_object_get_string(name_obj), NULL, 0);
			}
		}

		json_object_put(root);

		return 0;
	}

	return ENOENT;
}

static int fs_getattr(const char *path, struct stat *st)
{
	// printf("  DEBUG: fs_getattr: %s", path);

	fs_github_path fgp;
	int fgp_parse_result;

	if ((fgp_parse_result = fs_github_path_parse_from_path(&fgp, path)) != 0) {
		return fgp_parse_result;
	}

	printf("\nUSER:>>%s<<(%zu)\n", fgp.user.value, fgp.user.len);
	printf("\nREPO:>>%s<<(%zu)\n", fgp.repo.value, fgp.repo.len);

	// if (strcmp(path, "/") == 0) {
	if (fgp.user.value == NULL) {
		st->st_blksize = 512;
		st->st_mode = S_IFDIR | 0555;
		st->st_nlink = 3;

		return 0;
	}

	//if (strcmp(path, "/jd297") == 0) {
	if (fgp.user.value != NULL && fgp.repo.value == NULL) {
		// TODO check if user exists
		// 	check if cache hits
		//  else lookup from api
		// if not exists then -ENOENT
	
		st->st_mode = S_IFDIR | 0555;
		st->st_blksize = 512;
		st->st_nlink = 2;

		return 0;
	}

	return -ENOENT;
}

struct fuse_operations fsops = {
	.readdir = fs_readdir,
	.getattr = fs_getattr,
};

int main(int argc, char **argv)
{
	return fuse_main(argc, argv, &fsops, NULL);
}
