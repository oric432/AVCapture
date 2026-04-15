#include "BugAssembler.hpp"

using namespace VSCapture::Sync;
using namespace VSCapture::Error;
using namespace VSCapture;

static std::string join_remote(const std::string& first, const std::string& second) {
    if (first.empty()) {
        return second;
    }

    if (first.back() == '/') {
        return first + second;
    }

    return first + "/" + second;
}

BugAssembler::BugAssembler(Nfs::IFileBackend& nfs)
    : nfs_(nfs) {}

VoidResult BugAssembler::assemble(const AssemblerSpec& spec) {
    const std::filesystem::path temp_root =
        std::filesystem::temp_directory_path() / "VSCapture" / ("assemble_" + spec.bug_folder_name_);
    const std::filesystem::path in_dir = temp_root / "in";
    const std::filesystem::path work_dir = temp_root / "work";
    const std::filesystem::path out_dir = temp_root / "out";
    const std::filesystem::path final_dir = temp_root / "final" / spec.bug_folder_name_;

    const std::filesystem::path audio_zip = in_dir / "audio.zip";
    const std::filesystem::path video_zip = in_dir / "video.zip";

    std::error_code errc;
    std::filesystem::create_directories(in_dir, errc);
    std::filesystem::create_directories(work_dir, errc);
    std::filesystem::create_directories(out_dir, errc);
    std::filesystem::create_directories(final_dir, errc);

    auto cleanup = [&]() {
        std::error_code clean_errc;
        std::filesystem::remove_all(temp_root, clean_errc);
    };

    if (auto res = download_nfs_to_local(spec.nfs_audio_zip_, audio_zip); !res) {
        cleanup();
        return std::unexpected(res.error().with_context("Failed downloading nfs audio zip"));
    }


    if (auto res = download_nfs_to_local(spec.nfs_video_zip_, video_zip); !res) {
        cleanup();
        return std::unexpected(res.error().with_context("Failed downloading nfs video zip"));
    }

    const std::filesystem::path audio_extract = work_dir / "audio";
    const std::filesystem::path video_extract = work_dir / "video";
    std::filesystem::create_directories(audio_extract, errc);
    std::filesystem::create_directories(video_extract, errc);

    if (auto res = run_process("7z.exe", std::format(" x {} -o{} -y -r -bso0 -bse0", audio_zip.string(), audio_extract.string()));
        !res) {
        cleanup();
        return std::unexpected(res.error().with_context("Failed extracting audio zip"));
    }

    if (auto res = run_process("7z.exe", std::format(" x {} -o{} -y -r -bso0 -bse0", video_zip.string(), video_extract.string()));
        !res) {
        cleanup();
        return std::unexpected(res.error().with_context("Failed extracting video zip"));
    }

    auto audio_ts_r = find_first_ext(audio_extract, ".ts");
    if (!audio_ts_r) {
        cleanup();
        return std::unexpected(audio_ts_r.error().with_context("audio .ts not found"));
    }

    auto video_ts_r = find_first_ext(video_extract, ".ts");
    if (!video_ts_r) {
        cleanup();
        return std::unexpected(video_ts_r.error().with_context("audio .ts not found"));
    }

    auto audio_logs_r = find_logs_dir(audio_extract);
    auto video_logs_r = find_logs_dir(video_extract);

    const auto output_mp4 = out_dir / "output.mp4";

    {
        const auto args = std::format(
            " -y -hide_banner -loglevel fatal -i {} -i {} -map 0:v:0 -map 1:a:0 -c copy {}",
            video_ts_r->string(),
            audio_ts_r->string(),
            output_mp4.string());

        if (auto res = run_process("ffmpeg.exe", args); !res) {
            cleanup();
            return std::unexpected(res.error().with_context("ffmpeg mux Failed"));
        }
    }

    const std::filesystem::path final_audio_logs_dir = final_dir / "audio_logs";
    const std::filesystem::path final_video_logs_dir = final_dir / "video_logs";
    std::filesystem::create_directories(final_audio_logs_dir, errc);
    std::filesystem::create_directories(final_video_logs_dir, errc);

    std::filesystem::copy_file(
        output_mp4,
        final_dir / "output.mp4",
        std::filesystem::copy_options::overwrite_existing,
        errc);

    // std::filesystem::copy_file(
    //     *audio_ts_r,
    //     final_dir / audio_ts_r->filename(),
    //     std::filesystem::copy_options::overwrite_existing,
    //     errc);
    // std::filesystem::copy_file(
    //     *video_ts_r,
    //     final_dir / video_ts_r->filename(),
    //     std::filesystem::copy_options::overwrite_existing,
    //     errc);

    if (audio_logs_r) {
        if (auto res = copy_tree(*audio_logs_r, final_audio_logs_dir); !res) {
            Log::sync()->warn("Failed copying audio logs tree");
        }
    }

    if (video_logs_r) {
        if (auto res = copy_tree(*video_logs_r, final_video_logs_dir); !res) {
            Log::sync()->warn("Failed copying video logs tree");
        }
    }

    const std::filesystem::path final_zip = out_dir / (spec.bug_folder_name_ + ".zip");

    {
        const auto args = std::format(" a -tzip -mx=5 -y -r -bso0 -bse0 {} {}", final_zip.string(), final_dir.string());

        if (auto res = run_process("7z.exe", args); !res) {
            cleanup();
            return std::unexpected(res.error().with_context("Zipping final folder Failed"));
        }
    }

    const std::string remote_zip = join_remote(spec.nfs_out_dir_, spec.bug_folder_name_ + ".zip");
    const std::string remote_zip_tmp = remote_zip + ".tmp";

    if (auto res = upload_local_to_nfs(final_zip, remote_zip_tmp); !res) {
        cleanup();
        return std::unexpected(res.error().with_context("Failed uploading temp zip"));
    }
    if (auto res = nfs_.rename(remote_zip_tmp, remote_zip); !res) {
        cleanup();
        return std::unexpected(res.error().with_context("Failed renaming temp zip"));
    }

    if (auto res = nfs_.unlink(spec.nfs_audio_zip_); !res) {
        Log::sync()->warn("Failed unlinking audio zip from nfs: {}", res.error().what());
    }

    if (auto res = nfs_.unlink(spec.nfs_video_zip_); !res) {
        Log::sync()->warn("Failed unlinking video zip from nfs: {}", res.error().what());
    }

    cleanup();

    return {};
}


VoidResult BugAssembler::download_nfs_to_local(const std::string& nfs_path, const std::filesystem::path& local_path) {
    std::error_code errc;
    std::filesystem::create_directories(local_path.parent_path(), errc);

    auto fh_res = nfs_.open_read(nfs_path);
    if (!fh_res) {
        return std::unexpected(fh_res.error().with_context("Failed opening file handle for read"));
    }

    auto file_handle = fh_res.value();

    std::ofstream out(local_path, std::ios::binary | std::ios::trunc);
    if (!out) {
        if (auto res = nfs_.close(file_handle); !res) {
            Log::sync()->warn("Failed unlinking video zip from nfs: {}", res.error().what());
        }
        return std::unexpected(make_error().with_context("Failed opening local file for write"));
    }

    std::vector<char> buf(1024 * 1024);

    while (true) {
        auto res = nfs_.read(file_handle, buf.data(), buf.size());
        if (!res) {
            if (auto close_res = nfs_.close(file_handle); !close_res) {
                Log::sync()->warn("Failed unlinking video zip from nfs: {}", close_res.error().what());
            }
            return std::unexpected(res.error());
        }

        const auto bytes = res.value();
        if (bytes == 0) {
            break;
        }

        out.write(buf.data(), bytes);
        if (!out) {
            if (auto close_res = nfs_.close(file_handle); !close_res) {
                Log::sync()->warn("Failed unlinking video zip from nfs: {}", close_res.error().what());
            }
            return std::unexpected(make_error().with_context("Local write failed"));
        }
    }

    if (auto close_res = nfs_.close(file_handle); !close_res) {
        Log::sync()->warn("Failed unlinking video zip from nfs: {}", close_res.error().what());
    }

    return {};
}
VoidResult BugAssembler::upload_local_to_nfs(const std::filesystem::path& local_path, const std::string& nfs_path) {
    std::ifstream input(local_path, std::ios::binary);
    if (!input) {
        return std::unexpected(make_error().with_context("Failed opening local file for read"));
    }

    auto fh_res = nfs_.open_write_trunc(nfs_path);
    if (!fh_res) {
        return std::unexpected(fh_res.error().with_context("Failed opening file handle for read"));
    }

    auto file_handle = fh_res.value();
    std::vector<char> buf(1024 * 1024);
    while (input) {
        input.read(buf.data(), buf.size());
        const auto got = input.gcount();
        if (got <= 0) {
            break;
        }

        auto write = nfs_.write(file_handle, buf.data(), got);
        if (!write) {
            if (auto res = nfs_.close(file_handle); !res) {
                Log::sync()->warn("Failed unlinking video zip from nfs: {}", res.error().what());
            }
            return std::unexpected(write.error());
        }
    }

    if (auto res = nfs_.close(file_handle); !res) {
        Log::sync()->warn("Failed unlinking video zip from nfs: {}", res.error().what());
    }

    return {};
}

VoidResult BugAssembler::upload_tree(const std::filesystem::path& local_root, const std::string& nfs_root) {
    std::error_code errc;
    for (auto iter = std::filesystem::recursive_directory_iterator(local_root, errc);
         !errc && iter != std::filesystem::recursive_directory_iterator();
         ++iter) {
        const auto path = iter->path();
        const auto rel = std::filesystem::relative(path, local_root, errc);
        if (errc) {
            continue;
        }

        const auto remote = join_remote(nfs_root, rel.generic_string());

        if (iter->is_directory(errc)) {
            if (auto res = nfs_.mkdir_p(remote); !res) {
                return std::unexpected(res.error());
            }
            continue;
        }

        if (iter->is_regular_file(errc)) {
            if (auto res = upload_local_to_nfs(path, remote); !res) {
                return std::unexpected(res.error());
            }
        }
    }

    if (errc) {
        return std::unexpected(make_error().with_context("upload_tree iterator error: " + errc.message()));
    }

    return {};
}

VoidResult BugAssembler::run_process(const std::filesystem::path& exe, const std::string& args) {
    std::string cmd = "\"" + exe.string() + args + "\"";

    const int res = std::system(cmd.c_str());
    if (res != 0) {
        return std::unexpected(make_error().with_context("Process failed res=" + std::to_string(res)));
    }

    return {};
}

Result<std::filesystem::path> BugAssembler::find_first_ext(const std::filesystem::path& root, std::string_view ext) {
    std::error_code errc;
    for (auto iter = std::filesystem::recursive_directory_iterator(root, errc);
         !errc && iter != std::filesystem::recursive_directory_iterator();
         ++iter) {
        if (!iter->is_regular_file(errc)) {
            continue;
        }
        if (iter->path().extension() == ext) {
            return iter->path();
        }
    }

    return std::unexpected(make_error().with_context("Filed extension not found"));
}
Result<std::filesystem::path> BugAssembler::find_logs_dir(const std::filesystem::path& root) {
    std::error_code errc;
    for (auto iter = std::filesystem::recursive_directory_iterator(root, errc);
         !errc && iter != std::filesystem::recursive_directory_iterator();
         ++iter) {
        if (!iter->is_directory(errc)) {
            continue;
        }
        if (iter->path().filename() == "logs") {
            return iter->path();
        }
    }

    return std::unexpected(make_error().with_context("Logs folder not found"));
}
VoidResult BugAssembler::copy_tree(const std::filesystem::path& src, const std::filesystem::path& dst) {
    std::error_code errc;
    std::filesystem::create_directories(dst, errc);

    for (auto iter = std::filesystem::recursive_directory_iterator(src, errc);
         !errc && iter != std::filesystem::recursive_directory_iterator();
         ++iter) {
        const auto path = iter->path();
        const auto rel = std::filesystem::relative(path, src, errc);
        if (errc) {
            continue;
        }

        const auto out = dst / rel;

        if (iter->is_directory(errc)) {
            std::filesystem::create_directories(out, errc);
            continue;
        }

        if (iter->is_regular_file(errc)) {
            std::filesystem::create_directories(out.parent_path(), errc);
            std::filesystem::copy_file(path, out, std::filesystem::copy_options::overwrite_existing, errc);
        }
    }

    return {};
}