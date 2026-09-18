#include "tts_host/catalogue_download.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>

#include <nlohmann/json.hpp>

namespace {

using json = nlohmann::json;

void require(bool condition, const std::string &message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

tts_host::ConfigDocument make_config(const std::filesystem::path &config_path,
                                     const std::filesystem::path &config_schema_path,
                                     const std::filesystem::path &registry_directory) {
  tts_host::ConfigDocument document;
  document.value = json{
      {"schemaVersion", 1},
      {"modelRegistry",
       {{"directories", json::array({registry_directory.generic_string()})},
        {"watchForChanges", false},
        {"idleUnloadSeconds", 600},
        {"maximumLoadedGpuModels", 1}}},
  };
  document.paths.config_path = config_path;
  document.paths.schema_path = config_schema_path;
  return document;
}

tts_host::CatalogueEntry demo_entry() {
  return tts_host::CatalogueEntry{
      "demo-download",
      "Demo Download",
      "kokoro-onnx",
      {"en"},
      tts_host::CatalogueLicense{"Apache-2.0", "https://example.invalid/licence", false},
      {
          tts_host::CatalogueFile{
              "model.onnx",
              "model",
              "https://example.invalid/model.onnx",
              "c87f3acf21c22f962354c76ae45ee824474c9ef4193b89d08770cae36f504c7c",
              25,  // strlen("fake kokoro model weights")
          },
          tts_host::CatalogueFile{
              "voices/demo.bin",
              "voice",
              "https://example.invalid/voices/demo.bin",
              "8a94a751e713a5a5668261a2dc1076c2ae860481f32960aa35d7bf25dc34bb57",
              20,  // strlen("fake voice embedding")
          },
      },
  };
}

// Simulates the network with a fixed byte string per URL, honoring
// resume_from_bytes/destination/total_bytes exactly the way fetch_url_to_file
// (WinHTTP) contracts to, so download_catalogue_entry's resume and
// verification logic is exercised without any real transport.
tts_host::HttpFetchFunction fake_fetcher(const std::map<std::string, std::string> &bodies) {
  return [bodies](const std::string &url, std::uint64_t resume_from_bytes,
                 const std::filesystem::path &destination, std::uint64_t total_bytes,
                 const tts_host::DownloadProgressCallback &progress) {
    const auto found = bodies.find(url);
    require(found != bodies.end(), "fake fetcher has no body for " + url);
    const std::string &body = found->second;
    require(body.size() == total_bytes, "fixture body size does not match the catalogue entry: " + url);
    require(resume_from_bytes <= body.size(), "resume offset past the end of the body: " + url);

    std::ofstream output(destination, resume_from_bytes > 0 ? (std::ios::binary | std::ios::app)
                                                            : (std::ios::binary | std::ios::trunc));
    require(static_cast<bool>(output), "fake fetcher could not open " + destination.string());
    output.write(body.data() + resume_from_bytes,
                static_cast<std::streamsize>(body.size() - resume_from_bytes));
    output.close();

    if (progress) {
      progress(tts_host::DownloadProgress{destination.filename().string(), body.size(), total_bytes});
    }
  };
}

const std::map<std::string, std::string> &demo_bodies() {
  static const std::map<std::string, std::string> bodies = {
      {"https://example.invalid/model.onnx", "fake kokoro model weights"},
      {"https://example.invalid/voices/demo.bin", "fake voice embedding"},
  };
  return bodies;
}

void a_fresh_download_installs_the_verified_package(const std::filesystem::path &schema_dir,
                                                     const std::filesystem::path &work_dir) {
  const auto registry = work_dir / "fresh" / "registry";
  const auto config = make_config(work_dir / "fresh" / "config.json", schema_dir / "config.schema.json",
                                  registry);
  std::filesystem::create_directories(registry);

  const auto entry = demo_entry();
  const auto installed =
      tts_host::download_catalogue_entry(entry, registry, config, fake_fetcher(demo_bodies()), nullptr);

  require(installed.id == entry.id, "download reported the wrong id");
  require(installed.package_path == registry / entry.id, "download installed to the wrong path");
  require(std::filesystem::is_regular_file(installed.package_path / "model.onnx"),
          "the model file was not installed");
  require(std::filesystem::is_regular_file(installed.package_path / "voices" / "demo.bin"),
          "the voice file was not installed");

  std::ifstream manifest_stream(installed.package_path / "model.json");
  require(static_cast<bool>(manifest_stream), "no model.json was generated");
  const auto manifest = json::parse(manifest_stream);
  require(manifest.at("id") == entry.id, "generated manifest has the wrong id");
  require(manifest.at("files").at("model") == "model.onnx", "generated manifest's model file is wrong");
  require(manifest.at("files").at("voice") == "voices/demo.bin", "generated manifest's voice file is wrong");

  // No leftover staging directory.
  require(!std::filesystem::exists(registry / (".demo-download.downloading")),
          "a staging directory was left behind after a successful download");
}

void a_partially_downloaded_file_is_resumed_not_refetched(const std::filesystem::path &schema_dir,
                                                          const std::filesystem::path &work_dir) {
  const auto registry = work_dir / "resume" / "registry";
  const auto config = make_config(work_dir / "resume" / "config.json", schema_dir / "config.schema.json",
                                  registry);
  std::filesystem::create_directories(registry);

  const auto staging = registry / ".demo-download.downloading";
  std::filesystem::create_directories(staging);
  {
    // Half of "fake kokoro model weights" already on disk, as if a prior
    // attempt was interrupted.
    std::ofstream partial(staging / "model.onnx", std::ios::binary);
    partial << "fake kokoro ";
  }

  const auto entry = demo_entry();
  const auto installed =
      tts_host::download_catalogue_entry(entry, registry, config, fake_fetcher(demo_bodies()), nullptr);

  std::ifstream model_stream(installed.package_path / "model.onnx", std::ios::binary);
  std::string model_contents((std::istreambuf_iterator<char>(model_stream)), std::istreambuf_iterator<char>());
  require(model_contents == "fake kokoro model weights",
          "resuming a partial file produced the wrong contents: " + model_contents);
}

void a_checksum_mismatch_is_rejected_and_cleaned_up(const std::filesystem::path &schema_dir,
                                                    const std::filesystem::path &work_dir) {
  const auto registry = work_dir / "bad-checksum" / "registry";
  const auto config = make_config(work_dir / "bad-checksum" / "config.json",
                                  schema_dir / "config.schema.json", registry);
  std::filesystem::create_directories(registry);

  auto entry = demo_entry();
  entry.id = "bad-checksum-entry";
  entry.files.front().sha256 = "0000000000000000000000000000000000000000000000000000000000000000";

  try {
    tts_host::download_catalogue_entry(entry, registry, config, fake_fetcher(demo_bodies()), nullptr);
  } catch (const std::exception &error) {
    require(std::string(error.what()).find("checksum mismatch") != std::string::npos,
            std::string("expected a checksum mismatch error, got: ") + error.what());
    require(!std::filesystem::exists(registry / ".bad-checksum-entry.downloading" / "model.onnx"),
            "a file that failed checksum verification was left on disk");
    return;
  }
  throw std::runtime_error("a checksum mismatch was not rejected");
}

void an_already_installed_entry_is_rejected_before_fetching(const std::filesystem::path &schema_dir,
                                                             const std::filesystem::path &work_dir) {
  const auto registry = work_dir / "already-installed" / "registry";
  const auto config = make_config(work_dir / "already-installed" / "config.json",
                                  schema_dir / "config.schema.json", registry);
  const auto entry = demo_entry();
  std::filesystem::create_directories(registry / entry.id);

  bool fetch_called = false;
  const tts_host::HttpFetchFunction counting_fetch =
      [&fetch_called](const std::string &, std::uint64_t, const std::filesystem::path &, std::uint64_t,
                     const tts_host::DownloadProgressCallback &) { fetch_called = true; };

  try {
    tts_host::download_catalogue_entry(entry, registry, config, counting_fetch, nullptr);
  } catch (const std::exception &error) {
    require(std::string(error.what()).find("already installed") != std::string::npos,
            std::string("expected an already-installed error, got: ") + error.what());
    require(!fetch_called, "download fetched files for an entry already installed");
    return;
  }
  throw std::runtime_error("downloading an already-installed entry was not rejected");
}

void sha256_matches_known_test_vectors() {
  const auto temp_dir = std::filesystem::temp_directory_path() / "tts-host-sha256-vector-test";
  std::filesystem::create_directories(temp_dir);
  const auto path = temp_dir / "abc.txt";
  {
    std::ofstream output(path, std::ios::binary);
    output << "abc";
  }
  const auto digest = tts_host::sha256_hex_of_file(path);
  require(digest == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
          "sha256_hex_of_file('abc') did not match the known test vector, got: " + digest);
  std::filesystem::remove_all(temp_dir);
}

}  // namespace

int main(int argc, char **argv) {
  try {
    require(argc == 3, "usage: tts-host-catalogue-download-tests <schemas-directory> <work-directory>");
    const std::filesystem::path schemas_directory(argv[1]);
    const std::filesystem::path work_directory(argv[2]);
    std::filesystem::remove_all(work_directory);
    std::filesystem::create_directories(work_directory);

    sha256_matches_known_test_vectors();
    a_fresh_download_installs_the_verified_package(schemas_directory, work_directory);
    a_partially_downloaded_file_is_resumed_not_refetched(schemas_directory, work_directory);
    a_checksum_mismatch_is_rejected_and_cleaned_up(schemas_directory, work_directory);
    an_already_installed_entry_is_rejected_before_fetching(schemas_directory, work_directory);
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
  return 0;
}
