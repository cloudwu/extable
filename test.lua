local extable = require "extable"

local t = extable.open "data.lua"

print(t.a)
print(t.b)
print(t.c)

for k,v in pairs(t.c) do
	print(k,v)
end

for k,v in pairs(t) do
	print(k,v)
end

