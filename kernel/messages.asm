; kernel/messages.asm - shared message strings
; Used by multiple kernel modules (fs, dir, command)

fs_init_error_msg db 'File system initialization failed.', 13, 10, 0
missing_param_msg db 'Missing filename.', 13, 10, 0
file_not_found_msg:
    db 'File not found', 13, 10, 0
read_error_msg db 'Read error', 13, 10, 0
not_program_msg db 'Not a program.', 13, 10, 0
empty_file_msg db 'File is empty.', 13, 10, 0
prog_too_large_msg db 'Program too large.', 13, 10, 0

files_found_msg db ' files found', 13, 10, 0
