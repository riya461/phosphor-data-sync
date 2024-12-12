gtest_dep = dependency('gtest', main : true, required : true)

test_source_files = [
        'data_sync_config_test',
    ]

foreach test_file : test_source_files
    test(
        'test_' + test_file.underscorify(),
        executable(
            'test-' + test_file.underscorify(),
            test_file + '.cpp',
            rbmc_data_sync_sources,
            dependencies : [
                gtest_dep,
                rbmc_data_sync_dependencies,
            ],
            include_directories: inc_dir,
        )
    )
endforeach
