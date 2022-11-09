{
   "targets":[
      {
         "target_name":"libfosscordcdn",
         "include_dirs":[
            "$(NODE_ROOT)/src",
            "$(NODE_ROOT)/deps/v8",
            "$(NODE_ROOT)/deps/uv"
         ],
         "conditions":[
            [
               "OS=='win' and target_arch=='x64'",
               {
                  "include_dirs":[
                     "<(module_root_dir)/ffmpeg/win64/include"
                  ],
                  "libraries":[
                     "<(module_root_dir)/ffmpeg/win64/lib/avcodec.lib",
                     "<(module_root_dir)/ffmpeg/win64/lib/avformat.lib",
                     "<(module_root_dir)/ffmpeg/win64/lib/avutil.lib"
                  ],
               },
               "OS=='linux' and target_arch=='x64'",
               {
                  "include_dirs":[
                     "<(module_root_dir)/ffmpeg/linux64/include"
                  ],
                  "libraries":[
                     "<(module_root_dir)/ffmpeg/linux64/lib/libavcodec.so",
                     "<(module_root_dir)/ffmpeg/linux64/lib/libavformat.so",
                     "<(module_root_dir)/ffmpeg/linux64/lib/libavutil.so"
                  ],
               }
            ]
         ],
         "configurations":{
            "Debug":{
               "msvs_settings":{
                  "VCCLCompilerTool":{
                     "RuntimeLibrary":1
                  },
               }
            },
            "Release":{
               "msvs_settings":{
                  "VCCLCompilerTool":{
                     "RuntimeLibrary":0
                  }
               }
            }
         }
      }
   ]
}