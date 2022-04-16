io.write("{")
for i = 1, #arg do
   if i > 1 then
      io.write(", ")
   end
   io.write(string.format("%q", arg[i]))
end
io.write("}\n")
