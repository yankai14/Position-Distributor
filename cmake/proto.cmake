include(FetchContent)

FetchContent_Declare(
        protobuf
        GIT_REPOSITORY https://github.com/protocolbuffers/protobuf.git
        GIT_TAG v21.12
)

FetchContent_MakeAvailable(protobuf)