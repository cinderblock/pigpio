{
    "targets": [
        {
            "target_name": "pigpio",
            "cflags!": [""],
            "cflags_cc!": [""],
            "include_dirs": ["<!(node -p \"require('node-addon-api').include_dir\")"],
            "dependencies": ["<!(node -p \"require('node-addon-api').gyp\")"],
            "xcode_settings": {
                "GCC_ENABLE_CPP_EXCEPTIONS": "YES",
                "CLANG_CXX_LIBRARY": "libc++",
                "MACOSX_DEPLOYMENT_TARGET": "10.7",
            },
            "msvs_settings": {
                "VCCLCompilerTool": {"ExceptionHandling": 1},
            },
            "include_dirs": [],
            "sources": ["./src/pigpio.cc"],
            "link_settings": {"libraries": ["-lpigpio"]},
            "conditions": [
                [
                    '"<!(echo $V)" != "1"',
                    {
                        "cflags": [
                            "-Wno-deprecated-declarations",
                            "-Wno-cast-function-type",
                        ],
                    },
                ],
                [
                    'OS=="win"',
                    {
                        "include_dirs+": ["./dev/"],
                    },
                ],
            ],
        },
    ],
}
