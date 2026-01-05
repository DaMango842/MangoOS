; filesys.asm
fs_init:
    ; 读取 FS Header
    ret

fs_list:
    ; 列目录（for dir 命令）
    ret

fs_find:
    ; SI = filename
    ; 返回：
    ;   CF=0 found
    ;   ES:BX -> FileEntry
    ret

fs_load:
    ; SI = filename
    ; ES:BX = load addr
    ; 返回 CF
    ret
