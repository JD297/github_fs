#include <errno.h>
#include <fuse.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include <curl/curl.h>
#include <json-c/json.h>

struct json_object* read_json_from_file(FILE* file)
{
	char* json_string = NULL;

	const size_t block = 4096;
	size_t nread_total = 0;

	do {
		json_string = (char *)realloc(json_string, nread_total + block);

		nread_total += fread(json_string + nread_total, sizeof(char), block, file);

		//printf(">>>%s<<<\n", json_string);
	} while (feof(file) == 0);

	struct json_object* json_obj = json_tokener_parse(json_string);

	//printf(">>\n%s\n<<\n", json_string);

	//free(json_string);

	return json_obj;
}

/*
size_t write_callback(char *ptr, size_t size, size_t nmemb, void *userdata)
{

}
*/

static int fs_readdir(const char *path, void *data, fuse_fill_dir_t filler, off_t off, struct fuse_file_info *ffi)
{
	(void)off;
	(void)ffi;

	printf("FILE: %s\n", path);

	if (strcmp(path, "/jd297") == 0) {
		if (off != 0) { // TODO: research this! but the hack for now works! :)
			return 0;
		}

		FILE *file = tmpfile();

		CURL *curl = curl_easy_init();
		curl_easy_setopt(curl, CURLOPT_URL, "https://api.github.com/users/JD297/repos");
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

		size_t array_length = json_object_array_length(root);

		for (size_t i = 0; i < array_length; i++) {
			struct json_object* json_obj = json_object_array_get_idx(root, i);

			struct json_object* name_obj;

			if (json_object_object_get_ex(json_obj, "name", &name_obj)) {
				// printf("NAME: %s\n", json_object_get_string(name_obj));
				filler(data, json_object_get_string(name_obj), NULL, 0);
			}
		}

		json_object_put(root);

		return 0;
	}

	if (strcmp(path, "/") != 0) {
		return ENOENT;
	}

	// curl https://api.github.com/search/users?q=jd297

	filler(data, ".", NULL, 0);
	filler(data, "..", NULL, 0);
	filler(data, "jd297", NULL, 0);


	return 0;
}

/*
static int fs_read(const char *path, char *buf, size_t size, off_t off, struct fuse_file_info *ffi)
{
	(void)path;
	(void)ffi;

	size_t len;
	const char *file_contents = "fuse filesystem example\n";

	len = strlen(file_contents);

	if (off < len) {
		if (off + size > len) {
			size = len - off;
		}

		memcpy(buf, file_contents + off, size);
	} else {
		size = 0;
	}

	return size;
}

static int fs_open(const char *path, struct fuse_file_info *ffi)
{
	return 0;

	if (strncmp(path, "/file", 10) != 0) {
		return -ENOENT;
	}

	if ((ffi->flags & 3) != O_RDONLY) {
		return -EACCES;
	}

	return 0;
}
*/

static int fs_getattr(const char *path, struct stat *st)
{
	if (strcmp(path, "/") == 0) {
		st->st_blksize = 512;
		st->st_mode = S_IFDIR | 0555;
		st->st_nlink = 3;
	}
	else if (strcmp(path, "/jd297") == 0) {
		st->st_mode = S_IFDIR | 0555;
		st->st_blksize = 512;
		st->st_nlink = 2;
	} else {
		return -ENOENT;
	}

	return 0;
}

struct fuse_operations fsops = {
	.readdir = fs_readdir,
	//.read = fs_read,
	//.open = fs_open,
	.getattr = fs_getattr,
};

int main(int argc, char **argv)
{
	return fuse_main(argc, argv, &fsops, NULL);
}
