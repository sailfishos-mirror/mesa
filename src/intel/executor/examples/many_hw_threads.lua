local hw_threads = 4
local simd_width = devinfo.ver >= 20 and 16 or 8
local total_lanes = hw_threads * simd_width

local r = execute {
  src = string.format([[
    @param hw_threads %d
    @id r4
    @write r4 r4
    @eot
  ]], hw_threads),
}

local expected = {}
for i = 0, total_lanes - 1 do
  expected[i] = i
end

print("result")
dump(r, total_lanes)

print("expected")
dump(expected, total_lanes)

for i = 0, total_lanes - 1 do
  assert(r[i] == expected[i],
         string.format("r[%d]=0x%08x expected 0x%08x",
                       i, r[i], expected[i]))
end

print("OK")
