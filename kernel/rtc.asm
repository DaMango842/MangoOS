; ============================================================================
; kernel/rtc.asm - 实机级 RTC/CMOS 驱动 (MangoOS)
; 修复: NMI管理, I/O延迟, 双重校验, BCD/二进制, 12/24小时制, 世纪回退
; ============================================================================

; ------------------------------ 常量 -----------------------------------------
RTC_INDEX   equ 0x70
RTC_DATA    equ 0x71
RTC_REG_A   equ 0x0A
RTC_REG_B   equ 0x0B
RTC_SEC     equ 0x00
RTC_MIN     equ 0x02
RTC_HOUR    equ 0x04
RTC_DAY     equ 0x07
RTC_MON     equ 0x08
RTC_YEAR    equ 0x09
RTC_CENTURY equ 0x32

; 数据区在文件末尾 (代码保持连续, 防止执行流误入数据)

; ----------------------------------------------------------------------------
; io_delay - 短延迟 (实机必须)
; ----------------------------------------------------------------------------
io_delay:
    out 0x80, al       ; 写入哑端口
    ret

; ----------------------------------------------------------------------------
; read_cmos_byte - 读取 CMOS 寄存器 (自动禁用 NMI)
; 输入: AL = 寄存器号
; 输出: AL = 数据
; ----------------------------------------------------------------------------
read_cmos_byte:
    push bx
    mov bl, al          ; 保存寄存器号
    or  bl, 0x80        ; 禁用 NMI (bit7=1)
    mov al, bl
    out RTC_INDEX, al
    call io_delay
    in  al, RTC_DATA
    call io_delay
    pop bx
    ret

; ----------------------------------------------------------------------------
; wait_rtc_update - 等待 RTC 更新完成 (带超时)
; ----------------------------------------------------------------------------
wait_rtc_update:
    push ax
    push cx
    mov cx, 0xFFFF      ; 超时计数器
.loop:
    mov al, RTC_REG_A | 0x80
    out RTC_INDEX, al
    call io_delay
    in  al, RTC_DATA
    call io_delay
    test al, 0x80       ; 检查 UIP (bit7)
    jz  .done
    loop .loop          ; 超时则退出
.done:
    pop cx
    pop ax
    ret

; ----------------------------------------------------------------------------
; bcd_to_bin - BCD 转 二进制 (0-99)
; 输入: AL = BCD, 输出: AL = 二进制
; ----------------------------------------------------------------------------
bcd_to_bin:
    push cx
    mov cl, al
    shr al, 4
    mov ch, 10
    mul ch
    and cl, 0x0F
    add al, cl
    pop cx
    ret

; ----------------------------------------------------------------------------
; get_rtc_datetime - 读取日期时间到 rtc_* 变量 (实机安全)
; 输出: rtc_sec/min/hour/day/mon/year/cent 全部为二进制值 (24小时制)
; ----------------------------------------------------------------------------
get_rtc_datetime:
    cli                         ; 关中断, 防止被打断

    push ax
    push bx
    push cx
    push dx
    push si

    ; ---------- 重试读取 (最多 3 次, 确保数据一致) ----------
    mov si, 3                   ; 重试计数器
.retry:
    call wait_rtc_update

    ; 第一次读取全部
    mov al, RTC_SEC
    call read_cmos_byte
    mov dl, al                  ; DL = 秒1

    mov al, RTC_MIN
    call read_cmos_byte
    mov dh, al                  ; DH = 分

    mov al, RTC_HOUR
    call read_cmos_byte
    mov bl, al                  ; BL = 时(原始)

    mov al, RTC_DAY
    call read_cmos_byte
    mov bh, al                  ; BH = 日

    mov al, RTC_MON
    call read_cmos_byte
    mov cl, al                  ; CL = 月

    mov al, RTC_YEAR
    call read_cmos_byte
    mov ch, al                  ; CH = 年

    mov al, RTC_CENTURY
    call read_cmos_byte
    mov [rtc_cent_tmp], al       ; 暂存原始世纪 (SI 仍用作重试计数器)

    ; 第二次读取秒数, 校验是否一致
    mov al, RTC_SEC
    call read_cmos_byte
    cmp al, dl
    je  .data_ok                ; 一致则跳转

    dec si
    jnz .retry                  ; 不一致则重试

    ; 重试 3 次都失败 → 数据无效, 全部置 0xFF
    mov byte [rtc_sec], 0xFF
    mov byte [rtc_min], 0xFF
    mov byte [rtc_hour], 0xFF
    mov byte [rtc_day], 0xFF
    mov byte [rtc_mon], 0xFF
    mov byte [rtc_year], 0xFF
    mov byte [rtc_cent], 0xFF
    jmp .restore_nmi

.data_ok:
    ; ---------- 读取寄存器 B 标志 ----------
    mov al, RTC_REG_B
    call read_cmos_byte
    mov ah, al                  ; AH = 标志 (bit2:0=BCD,1=二进制; bit1:1=12小时制)

    ; ---------- 转换秒 (BCD→二进制 或 直接保留) ----------
    mov al, dl
    test ah, 0x04
    jnz .sec_bin
    call bcd_to_bin
.sec_bin:
    mov [rtc_sec], al

    ; ---------- 转换分 ----------
    mov al, dh
    test ah, 0x04
    jnz .min_bin
    call bcd_to_bin
.min_bin:
    mov [rtc_min], al

    ; ---------- 转换小时 (处理 12/24 小时制) ----------
    mov al, bl                  ; 原始小时
    test ah, 0x02               ; 检查 12小时制标志
    jz  .hour_24

    ; ---- 12小时制 ----
    mov [rtc_pm_tmp], al         ; 暂存原始小时 (含 PM 位)
    and al, 0x7F                 ; 去掉 PM 位
    test ah, 0x04
    jnz .hour_12_bin
    call bcd_to_bin
.hour_12_bin:
    test byte [rtc_pm_tmp], 0x80
    jz  .hour_am
    ; PM: 12点不变, 其余 +12
    cmp al, 12
    jae .hour_done
    add al, 12
    jmp .hour_done
.hour_am:
    ; AM: 12点变 0
    cmp al, 12
    jne .hour_done
    xor al, al
    jmp .hour_done

.hour_24:
    ; ---- 24小时制 ----
    test ah, 0x04
    jnz .hour_24_bin
    call bcd_to_bin
.hour_24_bin:
.hour_done:
    mov [rtc_hour], al

    ; ---------- 转换日 ----------
    mov al, bh
    test ah, 0x04
    jnz .day_bin
    call bcd_to_bin
.day_bin:
    mov [rtc_day], al

    ; ---------- 转换月 ----------
    mov al, cl
    test ah, 0x04
    jnz .mon_bin
    call bcd_to_bin
.mon_bin:
    mov [rtc_mon], al

    ; ---------- 转换年 ----------
    mov al, ch
    test ah, 0x04
    jnz .year_bin
    call bcd_to_bin
.year_bin:
    mov [rtc_year], al

    ; ---------- 处理世纪 ----------
    ; 如果读取到 0x00 或 0xFF 则视为无效, 保留默认 20
    mov al, [rtc_cent_tmp]
    cmp al, 0x00
    je  .cent_done
    cmp al, 0xFF
    je  .cent_done
    ; 有效世纪, 转换
    test ah, 0x04
    jnz .cent_bin
    call bcd_to_bin
.cent_bin:
    mov [rtc_cent], al
.cent_done:

.restore_nmi:
    ; ---------- 恢复 NMI (重新启用中断) ----------
    ; 写 0x70 最高位为 0, 索引任意 (这里用 0x0A)
    mov al, 0x0A                ; 不带 0x80, 启用 NMI
    out RTC_INDEX, al
    call io_delay

    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    sti                         ; 恢复中断
    ret

; ------------------------------ 数据区 (文件末尾) -----------------------------
; 数据集中放在所有函数之后, 代码区保持连续,
; 防止任何 fallthrough / 误跳转把数据字节当成指令执行
rtc_sec   db 0
rtc_min   db 0
rtc_hour  db 0
rtc_day   db 0
rtc_mon   db 0
rtc_year  db 0
rtc_cent  db 20       ; 默认世纪 (2000年代)
rtc_cent_tmp db 0     ; 暂存: 原始世纪 (转换前)
rtc_pm_tmp  db 0      ; 暂存: 12小时制 PM 标志