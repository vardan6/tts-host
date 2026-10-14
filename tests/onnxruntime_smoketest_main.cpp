// Proves the vendored ONNX Runtime toolchain links, loads a model, and runs
// inference end to end, ahead of wiring the real Kokoro-82M weights. Not part
// of the runner protocol: it takes a model path directly on argv, not JSON-RPC.
#include <onnxruntime_cxx_api.h>

#include <array>
#include <cstdlib>
#include <iostream>

int main(int argc, char **argv) {
  if (argc != 2) {
    std::cerr << "usage: " << argv[0] << " <path-to-onnx-model>\n";
    return EXIT_FAILURE;
  }

  try {
    Ort::Env env(ORT_LOGGING_LEVEL_WARNING, "tts-host-onnxruntime-smoketest");
    Ort::SessionOptions session_options;
#ifdef _WIN32
    const std::wstring model_path(argv[1], argv[1] + std::string(argv[1]).size());
    Ort::Session session(env, model_path.c_str(), session_options);
#else
    Ort::Session session(env, argv[1], session_options);
#endif

    std::array<float, 1> input_values{42.0f};
    std::array<int64_t, 1> input_shape{1};
    Ort::MemoryInfo memory_info = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
    Ort::Value input_tensor = Ort::Value::CreateTensor<float>(
        memory_info, input_values.data(), input_values.size(), input_shape.data(), input_shape.size());

    constexpr const char *input_names[] = {"input"};
    constexpr const char *output_names[] = {"output"};
    auto outputs =
        session.Run(Ort::RunOptions{nullptr}, input_names, &input_tensor, 1, output_names, 1);

    const float output_value = *outputs.front().GetTensorMutableData<float>();
    if (output_value != input_values[0]) {
      std::cerr << "expected identity output " << input_values[0] << " but got " << output_value << '\n';
      return EXIT_FAILURE;
    }

    std::cout << "ONNX Runtime smoke test OK: identity(" << input_values[0] << ") = " << output_value << '\n';
    return EXIT_SUCCESS;
  } catch (const Ort::Exception &error) {
    std::cerr << "ONNX Runtime error: " << error.what() << '\n';
    return EXIT_FAILURE;
  }
}
