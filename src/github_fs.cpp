#include <errno.h>
#include <fuse.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>

#include <string>
#include <iostream>
#include <map>
#include <memory>

#include <curl/curl.h>
#include <nlohmann/json.hpp>

namespace github_fs
{
	class fs_node
	{
	public:
		std::shared_ptr<struct stat> st;
		std::shared_ptr<std::map<std::string, std::shared_ptr<fs_node>>> nodes;

		fs_node()
		{
			this->st = std::make_shared<struct stat>();
			this->nodes = std::make_shared<std::map<std::string, std::shared_ptr<fs_node>>>();
		}
	};
}

std::shared_ptr<github_fs::fs_node> fs_root_node;

class github_fs_path
{
public:
	std::string user;
	std::string repo;
	std::string branch;
	std::string path;

	github_fs_path(const char *path)
	{
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
					this->user = std::string(current).substr(0, next - current);
				} break;
				case 1: {
					this->repo = std::string(current).substr(0, next - current);
				} break;
				case 2: {
					this->branch = std::string(current).substr(0, next - current);
				} break;
				case 3: {
					this->path = std::string(current);

					return;
				} break;
			}

			index++;

			current = (*next == '/') ? next + 1 : next;
		}
	}
};

namespace github_fs::api
{
	typedef struct response {
		long code;
		nlohmann::json json;
	} response;
}

github_fs::api::response github_api_users_repos(github_fs_path gfp)
{
	FILE *file = tmpfile();

	CURL *curl = curl_easy_init();

	std::string url = "https://api.github.com/users/" + gfp.user + "/repos";

	curl_easy_setopt(curl, CURLOPT_URL, url.c_str());

	curl_easy_setopt(curl, CURLOPT_USERAGENT, "github_fs");
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, file);

	github_fs::api::response res;

	curl_easy_perform(curl);

	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &res.code);

	curl_easy_cleanup(curl);

	rewind(file);

	res.json = nlohmann::json::parse(file);

	return res;
}

github_fs::api::response github_api_repos_branches(github_fs_path gfp)
{
	FILE *file = tmpfile();

	CURL *curl = curl_easy_init();

	std::string url = "https://api.github.com/repos/" + gfp.user + "/" + gfp.repo + "/branches";

	curl_easy_setopt(curl, CURLOPT_URL, url.c_str());

	curl_easy_setopt(curl, CURLOPT_USERAGENT, "github_fs");
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, file);

	github_fs::api::response res;

	curl_easy_perform(curl);

	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &res.code);

	curl_easy_cleanup(curl);

	rewind(file);

	res.json = nlohmann::json::parse(file);

	return res;
}

github_fs::api::response github_api_repos_contents(github_fs_path gfp)
{
	FILE *file = tmpfile();

	CURL *curl = curl_easy_init();

	std::string url = "https://api.github.com/repos/" + gfp.user + "/" + gfp.repo + "/contents/" + gfp.path  + "?ref=" + gfp.branch;

	std::cout << std::endl << url << std::endl;

	curl_easy_setopt(curl, CURLOPT_URL, url.c_str());

	curl_easy_setopt(curl, CURLOPT_USERAGENT, "github_fs");
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, file);

	github_fs::api::response res;

	curl_easy_perform(curl);

	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &res.code);

	curl_easy_cleanup(curl);

	rewind(file);

	res.json = nlohmann::json::parse(file);

	return res;
}

int github_fs_get_node(const char *path, std::shared_ptr<github_fs::fs_node> *node)
{
	github_fs_path gfp(path);

	/* ROOT */
	if (gfp.user.empty()) {
		*node = fs_root_node;

		return 0;
	}

	/* USER */
	if (gfp.repo.empty()) {
		std::shared_ptr<github_fs::fs_node> fs_user_node = (*fs_root_node->nodes)[std::string(gfp.user)];

		if (fs_user_node.use_count() <= 0) {
			fs_user_node = std::make_shared<github_fs::fs_node>();
		
			std::cout << std::endl << "CACHE LOOKUP FAILED | USER: " << path << std::endl;

			github_fs::api::response res = github_api_users_repos(gfp);

			if (res.code >= 300) {
				std::cout  << std::endl << "API ERROR USER: [" << res.code << "]" << std::endl;
				return -ENOENT;
			}

			size_t links = 0;

			for (const auto& item : res.json) {
				++links;

				(*fs_user_node->nodes)[item["name"].get_ref<const std::string&>()] = std::make_shared<github_fs::fs_node>();
			}

			fs_user_node->st->st_mode = S_IFDIR | 0555;
			fs_user_node->st->st_blksize = 512;
			fs_user_node->st->st_nlink = 2 + links; // 2 + NUM of Repos found
			fs_user_node->st->st_size = 4096; // TODO WORKAROUND

			(*fs_user_node->nodes)["."] = fs_user_node;
			(*fs_user_node->nodes)[".."] = fs_root_node;

			(*fs_root_node->nodes)[std::string(gfp.user)] = fs_user_node;

			++fs_root_node->st->st_nlink;
		} else {
			std::cout << std::endl << "CACHE LOOKUP HIT | USER: " << path << std::endl;
		}

		*node = fs_user_node;

		return 0;
	}

	/* REPOSITORY */
	if (gfp.branch.empty()) {
		std::shared_ptr<github_fs::fs_node> fs_user_node = (*fs_root_node->nodes)[std::string(gfp.user)];
		std::shared_ptr<github_fs::fs_node> fs_repo_node = (*fs_user_node->nodes)[std::string(gfp.repo)];

		if (fs_repo_node->st->st_nlink == 0) {
			std::cout << std::endl << "CACHE LOOKUP FAILED | REPO: " << path << std::endl;

			github_fs::api::response res = github_api_repos_branches(gfp);

			if (res.code >= 300) {
				std::cout  << std::endl << "API ERROR USER: [" << res.code << "]" << std::endl;
				return -ENOENT;
			}

			size_t links = 0;

			for (const auto& item : res.json) {
				++links;

				(*fs_repo_node->nodes)[item["name"].get_ref<const std::string&>()] = std::make_shared<github_fs::fs_node>();
			}

			fs_repo_node->st->st_mode = S_IFDIR | 0555;
			fs_repo_node->st->st_blksize = 512;
			fs_repo_node->st->st_nlink = 2 + links;
			fs_repo_node->st->st_size = 4096; // TODO WORKAROUND

			(*fs_repo_node->nodes)["."] = fs_repo_node;
			(*fs_repo_node->nodes)[".."] = fs_user_node;

			(*fs_user_node->nodes)[std::string(gfp.repo)] = fs_repo_node;
		} else {
			std::cout << std::endl << "CACHE LOOKUP HIT | REPO: " << path << std::endl;
		}

		*node = fs_repo_node;

		return 0;
	}

	/* BRANCH */
	if (gfp.path.empty()) {
		std::shared_ptr<github_fs::fs_node> fs_user_node = (*fs_root_node->nodes)[std::string(gfp.user)];
		std::shared_ptr<github_fs::fs_node> fs_repo_node = (*fs_user_node->nodes)[std::string(gfp.repo)];
		std::shared_ptr<github_fs::fs_node> fs_branch_node = (*fs_repo_node->nodes)[std::string(gfp.branch)];

		if (fs_branch_node->st->st_nlink == 0) {
			std::cout << std::endl << "CACHE LOOKUP FAILED | BRANCH: " << path << std::endl;

			github_fs::api::response res = github_api_repos_contents(gfp);

			if (res.code >= 300) {
				std::cout  << std::endl << "API ERROR USER: [" << res.code << "]" << std::endl;
			
				return -ENOENT;
			}

			size_t links = 0;

			for (const auto& item : res.json) {
				++links;

				std::shared_ptr<github_fs::fs_node> fs_file_node = std::make_shared<github_fs::fs_node>();

				
				if ((item["type"].get_ref<const std::string&>()).compare("file") == 0) {
					std::cout << std::endl << "FILE: " << item["name"].get_ref<const std::string&>() << std::endl;
					
					fs_file_node->st->st_mode = S_IFREG | 0444;
					fs_file_node->st->st_blksize = 512;
					fs_file_node->st->st_nlink = 1;
					//fs_file_node->st->st_size = item["size"].get<off_t>(); // TODO
				}

				(*fs_branch_node->nodes)[item["name"].get_ref<const std::string&>()] = fs_file_node;
			}

			fs_branch_node->st->st_mode = S_IFDIR | 0555;
			fs_branch_node->st->st_blksize = 512;
			fs_branch_node->st->st_nlink = 2 + links;
			fs_branch_node->st->st_size = 4096; // TODO WORKAROUND

			(*fs_branch_node->nodes)["."] = fs_branch_node;
			(*fs_branch_node->nodes)[".."] = fs_repo_node;

			(*fs_repo_node->nodes)[std::string(gfp.branch)] = fs_branch_node;
		} else {
			std::cout << std::endl << "CACHE LOOKUP HIT | BRANCH: " << path << std::endl;
		}

		*node = fs_branch_node;

		return 0;
	}

	/* PATH */
	if (!gfp.path.empty()) {
		std::shared_ptr<github_fs::fs_node> fs_user_node = (*fs_root_node->nodes)[std::string(gfp.user)];
		std::shared_ptr<github_fs::fs_node> fs_repo_node = (*fs_user_node->nodes)[std::string(gfp.repo)];
		std::shared_ptr<github_fs::fs_node> fs_branch_node = (*fs_repo_node->nodes)[std::string(gfp.branch)];

		std::shared_ptr<github_fs::fs_node> fs_previous_path_node = fs_branch_node;
		std::shared_ptr<github_fs::fs_node> fs_path_node;

		std::filesystem::path path(gfp.path);

		for (auto part = path.begin(); part != path.end();) {
			fs_path_node = (*fs_previous_path_node->nodes)[part->string()];

			if (++part != path.end()) {
				fs_previous_path_node = fs_path_node;
			}
		}

		if (fs_path_node->st->st_nlink == 0) {
			std::cout << std::endl << "CACHE LOOKUP FAILED | PATH: " << path << std::endl;

			github_fs::api::response res = github_api_repos_contents(gfp);

			if (res.code >= 300) {
				std::cout  << std::endl << "API ERROR PATH: [" << res.code << "]" << std::endl;
			
				return -ENOENT;
			}

			size_t links = 0;

			for (const auto& item : res.json) {
				++links;

				std::shared_ptr<github_fs::fs_node> fs_file_node = std::make_shared<github_fs::fs_node>();

				if ((item["type"].get_ref<const std::string&>()).compare("file") == 0) {
					std::cout << std::endl << "FILE: " << item["name"].get_ref<const std::string&>() << std::endl;
					
					fs_file_node->st->st_mode = S_IFREG | 0444;
					fs_file_node->st->st_blksize = 512;
					fs_file_node->st->st_nlink = 1;
					//fs_file_node->st->st_size = item["size"].get<off_t>(); // TODO
				}

				(*fs_path_node->nodes)[item["name"].get_ref<const std::string&>()] = fs_file_node;
			}

			fs_path_node->st->st_mode = S_IFDIR | 0555;
			fs_path_node->st->st_blksize = 512;
			fs_path_node->st->st_nlink = 2 + links;
			fs_path_node->st->st_size = 4096; // TODO WORKAROUND

			(*fs_path_node->nodes)["."] = fs_path_node;
			(*fs_path_node->nodes)[".."] = fs_previous_path_node;

			(*fs_previous_path_node->nodes)[path.filename().string()] = fs_path_node;
		} else {
			std::cout << std::endl << "CACHE LOOKUP HIT | PATH: " << path << std::endl;
		}

		*node = fs_path_node;

		return 0;
	}

	return -ENOENT;
}

int fs_readdir(const char *path, void *data, fuse_fill_dir_t filler, off_t off, struct fuse_file_info *ffi)
{
	(void)ffi;

	if (off != 0) { // TODO: research this! but the hack for now works! :)
		return 0;
	}

	github_fs_path gfp(path);

	std::shared_ptr<github_fs::fs_node> node;
	int res;

	if ((res = github_fs_get_node(path, &node)) != 0) {
		return res;
	}

	/* ROOT */
	if (gfp.user.empty()) {
		filler(data, "..", NULL, 0);
	}

	for (auto it = node->nodes->begin(); it != node->nodes->end(); ++it) {
		filler(data, it->first.c_str(), &(*it->second->st), 0);
	}

	return res;
}

int fs_getattr(const char *path, struct stat *st)
{
	github_fs_path gfp(path);

	std::shared_ptr<github_fs::fs_node> node;
	int res;

	if ((res = github_fs_get_node(path, &node)) != 0) {
		return res;
	}

	*st = *node->st;

	return 0;
}

struct fuse_operations fsops;

int main(int argc, char **argv)
{
	fs_root_node = std::make_shared<github_fs::fs_node>();

	fs_root_node->st->st_mode = S_IFDIR | 0555;
	fs_root_node->st->st_blksize = 512;
	fs_root_node->st->st_size = 0;
	fs_root_node->st->st_nlink = 2;
	fs_root_node->st->st_size = 4096; // TODO WORKAROUND

	(*fs_root_node->nodes)["."] = fs_root_node;

	fsops.getattr = fs_getattr;
	fsops.readdir = fs_readdir;

	return fuse_main(argc, argv, &fsops, NULL);
}
