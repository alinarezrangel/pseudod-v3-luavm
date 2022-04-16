local opts = {}
for i = 1, #arg, 2 do
   local a = arg[i]
   local b = arg[i + 1]
   assert(string.match(a, "%-.+") and a ~= "--" and a ~= "-")
   opts[a] = b
end

local function read(st)
   return load("return " .. st, st, "t", {})()
end

local cflags = table.concat(read(assert(opts["--cflags"], "--cflags not specified")), " ")
local clibs = table.concat(read(assert(opts["--clibs"], "--clibs not specified")), " ")
local version = assert(opts["--version"], "--version not specified")
local prefix = assert(opts["--prefix"], "--prefix not specified")
local includedir = assert(opts["--includedir"], "--includedir not specified")
local libdir = assert(opts["--libdir"], "--libdir not specified")

local function replace(var)
   if var == "CFLAGS" then
      return cflags
   elseif var == "CLIBS" then
      return clibs
   elseif var == "VERSION" then
      return version
   elseif var == "PREFIX" then
      return prefix
   elseif var == "INCLUDEDIR" then
      return includedir
   elseif var == "LIBDIR" then
      return libdir
   else
      error("unknown variable " .. var)
   end
end

local infile <close> = io.open(opts["-i"], "rb")
local outfile <close> = io.open(opts["-o"], "wb")

for line in infile:lines "l" do
   outfile:write((string.gsub(line, "$%((%w+)%)", replace)))
   outfile:write "\n"
end
