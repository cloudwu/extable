local extable = require "extable"

local t = extable.open "data.lua"

print("a=", t.a)
print("b=", t.b)

print("#c=", #t.c)
for k,v in pairs(t.c) do
	print("pairs c:",k,v)
end
for k,v in ipairs(t.c) do
	print("ipairs c:", k,v)
end

for k,v in pairs(t) do
	print("pairs", k,v)
end

