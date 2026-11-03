-- FCEUX Lua dump PPU memory
-- output: ppu.bin

local filename = "ppu.bin"

local file = io.open(filename, "wb")

if not file then
    print("Cannot open ppu.bin")
    return
end


function dump_ppu()

    local data = {}

    -- Pattern Table $0000-$1FFF
    for addr = 0x0000, 0x1FFF do
        data[#data+1] = ppu.readbyte(addr)
    end


    -- Nametable $2000-$2FFF
    for addr = 0x2000, 0x2FFF do
        data[#data+1] = ppu.readbyte(addr)
    end


    -- Palette RAM $3F00-$3F1F
    for addr = 0x3F00, 0x3F1F do
        data[#data+1] = ppu.readbyte(addr)
    end


    -- 写入文件
    for _,v in ipairs(data) do
        file:write(string.char(v))
    end


    file:flush()

end


while true do

    dump_ppu()

    -- 每帧执行一次
    emu.frameadvance()

end