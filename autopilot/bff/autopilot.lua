-- The autopilot through BackForceFeeder: run_vjoy.py --bff drives the
-- cabinet's controls from here, BFF keeping vJoy and the motor.
--
-- Install: copy into Documents\BackForceFeeder\Scripts, and load it from the
-- game's script (indy500.lua...) with loadscript("autopilot.lua"), or name it
-- as the profile's script. NOT TESTED YET on a cabinet.
--
-- UDP on the loopback only:
--   in,  port 8765: "AP steer accel brake buttons"
--        steer -1..1 (left..right), accel and brake 0..1, buttons a mask:
--        1 coin, 2 start, 4 gear up, 8 gear down
--   out, port 8766: "DRV n" for each drive board byte the emulator sends,
--        "TQ torque wheel" every 20 ms (BFF's torque and wheel position)
-- No packet for 300 ms: the axes and buttons go back to the cabinet.

-- The profile's raw axes (from 1) and vJoy buttons (from 1): set them to
-- the cabinet's. The axes take BFF's corrected percent, 0..1.
local AXIS_WHEEL, AXIS_ACCEL, AXIS_BRAKE = 1, 2, 3
local BUTTONS = { [1] = 1, [2] = 4, [4] = 9, [8] = 10 }   -- mask bit -> vJoy button
local PORT_IN, PORT_OUT = 8765, 8766

-- Started by BFF itself when this script loads (nil: started by hand). The
-- autopilot then launches the emulator and drives it through this script.
-- It is run_autopilot.bat in the folder above this script's, found from the
-- path BFF loaded the script from; set it by hand if BFF gives none.
local function autopilot_dir()
    local ok, info = pcall(debug.getinfo, 1, "S")
    local source = ok and info and info.source or ""
    return source:match("^@(.*)[\\/]bff[\\/][^\\/]*$")
end
local here = autopilot_dir()
local LAUNCH = here and (here .. [[\run_autopilot.bat]]) or nil
local LAUNCH_ARGS = "indy500 --bff"

local UdpClient = luanet.import_type('System.Net.Sockets.UdpClient')
local IPEndPoint = luanet.import_type('System.Net.IPEndPoint')
local IPAddress = luanet.import_type('System.Net.IPAddress')
local Encoding = luanet.import_type('System.Text.Encoding')

local udp = nil
local last_packet = 0
local holding = false
local ticks = 0

local function send(text)
    local bytes = Encoding.ASCII:GetBytes(text)
    udp:Send(bytes, bytes.Length, "127.0.0.1", PORT_OUT)
end

local function let_go()
    if holding then
        releaserawaxis(AXIS_WHEEL); releaserawaxis(AXIS_ACCEL); releaserawaxis(AXIS_BRAKE)
        for _, button in pairs(BUTTONS) do setjoybutton(button, 0) end
        holding = false
        console("autopilot: controls back to the cabinet")
    end
end

local Process = luanet.import_type('System.Diagnostics.Process')
local launched = nil

function enter()
    udp = UdpClient(IPEndPoint(IPAddress.Loopback, PORT_IN))
    console("autopilot: listening on UDP " .. PORT_IN)
    if LAUNCH then
        launched = Process.Start(LAUNCH, LAUNCH_ARGS)
        console("autopilot: started " .. LAUNCH .. " " .. LAUNCH_ARGS)
    end
end

function update()
    ticks = ticks + 1
    local now = os.clock()
    while udp.Available > 0 do
        local from = IPEndPoint(IPAddress.Any, 0)
        local bytes = udp:Receive(from)
        local text = Encoding.ASCII:GetString(bytes)
        local s, a, b, mask = string.match(text, "^AP (%S+) (%S+) (%S+) (%d+)")
        if s then
            s, a, b, mask = tonumber(s), tonumber(a), tonumber(b), tonumber(mask)
            setrawaxis(AXIS_WHEEL, math.min(1, math.max(0, 0.5 + 0.5 * s)))
            setrawaxis(AXIS_ACCEL, math.min(1, math.max(0, a)))
            setrawaxis(AXIS_BRAKE, math.min(1, math.max(0, b)))
            for bit, button in pairs(BUTTONS) do
                setjoybutton(button, (mask & bit) ~= 0 and 1 or 0)
            end
            holding = true
            last_packet = now
        end
    end
    if holding and now - last_packet > 0.3 then
        let_go()
    end
    if ticks % 4 == 0 then
        send(string.format("TQ %.4f %.4f", gettorque(), getwheelpos()))
    end
end

function onoutput(name, value)
    if name == "RawDrive" then
        send("DRV " .. value)
    end
end

function exit()
    let_go()
    if udp then udp:Close() end
    if launched and not launched.HasExited then launched:Kill() end
end
