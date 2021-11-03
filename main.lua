local re = require "re"
local v = require "fennelview"

local VER = {1, 0, 0}

local log = {
   min = 2
}

function log.log(level, fmt, ...)
   if level >= log.min then
      print(fmt:format(...))
   end
end

function log.dbg(fmt, ...) log.log(0, "[DBG] " .. fmt, ...) end
function log.info(fmt, ...) log.log(1, "[INFO] " .. fmt, ...) end
function log.warn(fmt, ...) log.log(2, "[WARN] " .. fmt, ...) end
function log.error(fmt, ...) log.log(3, "[ERROR] " .. fmt, ...) end

local WARNINGS = {
   {"increasing-locals", "inc-locals", {"increasing_locals"},
    "Advierte de cuando los índices de las locales no están en órden ascendente."},
   {"redefined-constant", "redef-const", {"redefined_constant"},
    "Advierte cuando se redefina una constante."},
   {"procedure-doesnt-exists", "undef-proc", {"procedure_doesnt_exists"},
    "Advierte cuando se refiera a un procedimiento que no exísta."},
   {"redefined-procedure", "redef-proc", {"redefined_procedure"},
    "Advierte cuando se redefina un procedimiento."},
   {"no-procedure-section", "no-proc-sec", {"no_procedure_section"},
    "Advierte si no hay sección de procedimientos."},
   {"no-constants-section", "no-const-sec", {"no_constant_section"},
    "Advierte si no hay sección de constantes."},
   {"no-code-section", "no-code-sec", {"no_code_section"},
    "Advierte si no hay sección de código."},
   {"future", "future", {"future"},
    "Advierte sobre cosas que van a cambiar en un futuro."},
   {"useful", "useful", {"redefined_constant", "redefined_procedure", "procedure_doesnt_exists", "no_procedure_section", "no_code_section", "future"},
    "Activa advertencias útiles durante el desarrollo."},
   {"all", "all", {"increasing_locals", "redefined_constant", "redefined_procedure", "procedure_doesnt_exists", "no_procedure_section", "no_constant_section", "no_code_section"},
    "Activa todas las advertencias."},
}

local enabledwarnings = {}

local function warnabout(warn, fmt, ...)
   if enabledwarnings[warn] then
      log.warn(fmt, ...)
   end
end

local grammar = [==[

program <- {| '' -> 'program' ws version (rs platform)? (rs section)* ws |} {}

s <- %s / comment
ws <- s*
rs <- s+
comment <- ";" [^%nl]*

str <- {| {:str: '"' {(escape / [^%nl"])*} '"' :} |}
escape <- "\" ([qntr0b] / "u" [0-9a-fA-F]^10)

id <- {| {:id: [0-9]+ :} |}
int <- {| {:int: "-"? [0-9]+ :} |}
flt <- {| {:flt: "-"? [0-9]+ "." [0-9]+ :} |}

version <- {| '' -> 'version'
              "PDVM" rs {[0-9]+} "." {[0-9]+} |}

platform <- {| '' -> 'platform'
               "PLATFORM" rs pexp |}
pexp <- {| "(" ws {"and" / "or" / "not"} ws (pexp (ws "," ws pexp)*)? ws ")" |}
      / str

section <- platformsec / cpoolsec / codesec / procsec / unknownsec

platformsec <- {| '' -> 'platform_section'
                  "SECTION" ws '"platform"' (rs extern)* rs "ENDSECTION" |}
extern <- {| '' -> 'extern-proc'
             "EXTERN" ws str ws "PROC" rs id rs "STACK" ws "(" ws int ws "->" ws int ws ")" |}

cpoolsec <- {| '' -> 'constant_pool_section'
               "SECTION" ws '"constant pool"' (rs constant)* rs "ENDSECTION" |}
constant <- {| '' -> 'constant'
                "#" id rs ( '' -> 'string'  "STRING" ws str
                          / '' -> 'bigint'  "BIGINT" ws int
                          / '' -> 'bigdec'  "BIGDEC" ws flt
                          )
            |}

codesec <- {| '' -> 'code_section'
              "SECTION" ws '"code"' code rs "ENDSECTION" |}
opcode <- {| {OP} (rs oparg (ws "," ws oparg)*)? |}
oparg <- nil / envs / flt / int / str
nil <- {| {:type: '' -> 'nil' :} 'NIL' |}
envs <- {| {:type: '' -> 'env' :} { 'ESUP' / 'EACT' } |}

code <- {| '' -> 'locals'  (rs local)* |}
        {| '' -> 'opcodes'  (rs opcode)* |}
local <- {| {"LOCAL"} rs (id / envs) |}

OP <- "LCONST" / "ICONST" / "FCONST" / "CALL" / "SUM" / "SUB"
    / "MUL" / "DIV" / "RETN" / "RET" / "DYNCALL"
    / "MKENV" / "MKCLZC" / "MKCLZ" / "MK0CLZ"
    / "IFRAME" / "EINIT" / "EFRAME" / "PRINTT"
    / "LSETC" / "LGETC" / "LSET" / "LGET"
    / "EGETC" / "ESETC" / "ESET" / "EGET"

procsec <- {| '' -> 'procedures_section'
              "SECTION" ws '"procedures"' (rs proc)* rs "ENDSECTION" |}
proc <- {| '' -> 'proc'
           "PROC" rs id {| '' -> 'params'  (rs param)* |} code rs "ENDPROC" |}
param <- {| {"PARAM"} rs (id / envs) |}

unknownsec <- "SECTION" ws str (ws token)* rs "SECTION"
token <- str / [^%s"]+

]==]

local function pv(...)
   for i = 1, select("#", ...) do
      local x = select(i, ...)
      if i > 1 then
         io.write("\t")
      end
      io.write(v(x))
   end
   print()
end


local function escapecstr(str)
   assert(type(str) == "string", "expected string")
   local function repl(st)
      if st == "\n" then
         return "\\n"
      elseif st == "\r" then
         return "\\r"
      elseif st == "\t" then
         return "\\t"
      elseif st == "'" then
         return "\\'"
      elseif st == "\"" then
         return "\\\""
      else
         return ("\\x%02x"):format(string.byte(st))
      end
   end
   return (string.gsub(str, "[^a-zA-Z0-9.+/^&$@ -]", repl))
end

local ESUP, EACT = {special = true, type = "ESUP"}, {special = true, type = "EACT"}

local function processoparg(tbl)
   assert(type(tbl) == "table", "expected table")
   if tbl.int then
      return tonumber(tbl.int)
   elseif tbl.flt then
      return tonumber(tbl.flt)
   elseif tbl.id then
      return tonumber(tbl.id)
   elseif tbl.type == "nil" then
      return nil
   elseif tbl.type == "env" then
      if tbl[1] == "ESUP" then
         return ESUP
      else
         return EACT
      end
   else
      local escapes = {
         ["q"] = "\"",
         ["n"] = "\n",
         ["t"] = "\t",
         ["r"] = "\r",
         ["0"] = "\0",
         ["b"] = "\\",
      }
      return string.gsub(tbl.str, "\\[qntr0b]", escapes)
   end
end

local function opcode(tbl)
   local r = { tbl[1] }
   for i = 2, #tbl do
      r[i] = processoparg(tbl[i])
   end
   return r
end


local function processcode(code)
   local res = { locals = {}, opcodes = {} }
   for i = 1, #code.locals do
      res.locals[i] = opcode(code.locals[i])
   end
   for i = 1, #code.opcodes do
      res.opcodes[i] = opcode(code.opcodes[i])
   end
   return res
end

local function sliceseq(seq, s, e)
   local r = {}
   for i = s, e do
      table.insert(r, seq[i])
   end
   return r
end

local function sectionstotable(program)
   local sections = {}
   for i = 2, #program do
      sections[program[i][1]] = sliceseq(program[i], 2, #program[i])
   end
   return sections
end

local function codetotable(code)
   local c = {}
   c.locals = sliceseq(code[1], 2, #code[1])
   c.opcodes = sliceseq(code[2], 2, #code[2])
   return c
end

local function makestack()
   return {
      values = {},

      push = function(self, v)
         self.values[#self.values + 1] = v
      end,

      pop = function(self)
         local v = self.values[#self.values]
         self.values[#self.values] = nil
         return v
      end,

      top = function(self)
         return self.values[#self.values]
      end,
   }
end

local function prepproc(proc)
   assert(proc[1] == "proc", "procedure required")
   local c = {}
   c.id = processoparg(proc[2])
   c.params = sliceseq(proc[3], 2, #proc[3])
   for i = 1, #c.params do
      c.params[i] = opcode(c.params[i])
   end
   c.locals = sliceseq(proc[4], 2, #proc[4])
   for i = 1, #c.locals do
      c.locals[i] = opcode(c.locals[i])
   end
   c.opcodes = sliceseq(proc[5], 2, #proc[5])
   for i = 1, #c.opcodes do
      c.opcodes[i] = opcode(c.opcodes[i])
   end
   return c
end

local function processconstpool(cpool)
   local pool = {}
   for i = 1, #cpool do
      local c = cpool[i]
      assert(c[1] == "constant", "expected constant declaration")
      local id = processoparg(c[2])
      if pool[id] ~= nil then
         warnabout("redefined_constant", "redefined constant %d", id)
      end
      pool[id] = {
         type = c[3],
         value = processoparg(c[4]),
      }
   end
   return pool
end


function attach_extra(tbl, extra)
   local function index(t, k)
      local v = extra[k]
      if v ~= nil then
         return v
      else
         return rawget(t, k)
      end
   end
   local r = {}
   for k, v in pairs(tbl) do
      r[k] = v
   end
   return setmetatable(r, {__index = index})
end

local toc = {}

local function schema(s)
   return function(op)
      local opi = 2
      for i = 1, #s do
         local c = string.sub(s, i, i)
         if c ~= "?" then
            local a = op[opi]
            opi = opi + 1
            local vnil = false
            if i > 1 and string.sub(s, i - 1, i - 1) == "?" then
               vnil = a == nil
            end
            if c == "i" then
               assert(vnil or math.type(a) == "integer", "expected integer")
            elseif c == "f" then
               assert(vnil or math.type(a) == "float", "expected float")
            elseif c == "n" then
               assert(vnil or type(a) == "number", "expected number")
            elseif c == "u" then
               assert(vnil or (type(a) == "number" and a >= 0), "expected non-negative number")
            elseif c == "s" then
               assert(vnil or type(a) == "string", "expected string")
            elseif c == "d" then
               assert(vnil or (math.type(a) == "integer" and a >= 0) or (type(a) == "table" and (a.type == "ESUP" or a.type == "EACT")), "expected id")
            else
               error("unrecognized schema value")
            end
         end
      end
   end
end

function toc.makeemitter()
   local emit = {
      statments = {},
   }

   function emit:_formatsingle(arg, spec)
      local optional = false
      if spec:sub(1, 1) == "?" then
         spec = spec:sub(2)
         optional = true
      end
      if spec == "int" then
         assert(not optional and math.type(arg) == "integer", "expected integer")
         -- Emit arg as a C integer. tostring() is not correct, but should do
         -- for now
         return tostring(arg)
      elseif spec == "flt" then
         assert(not optional and math.type(arg) == "float", "expected float")
         -- Same caveat
         return tostring(arg)
      elseif spec == "idname" then
         local isspecial = false
         if arg == ESUP or arg == EACT then
            isspecial = true
         end
         assert(isspecial or math.type(arg) == "integer" and arg >= 0, "expected integer")
         if arg == ESUP then
            return "PDCRT_NAME_ESUP"
         elseif arg == EACT then
            return "PDCRT_NAME_EACT"
         else
            return "name_" .. tostring(arg)
         end
      elseif spec == "idint" then
         local ornil, isspecial = false, false
         if optional then
            ornil = arg == nil
         end
         if arg == ESUP or arg == EACT then
            isspecial = true
         end
         assert(ornil or isspecial or (math.type(arg) == "integer" and arg >= 0), "expected integer")
         if arg == nil then
            return "PDCRT_ID_NIL"
         elseif arg == ESUP then
            return "PDCRT_ID_ESUP"
         elseif arg == EACT then
            return "PDCRT_ID_EACT"
         else
            return tostring(arg)
         end
      elseif spec == "strlit" then
         assert(type(arg) == "string", "expected string")
         return '"' .. escapecstr(arg) .. '"'
      else
         error("unknown specifier " .. spec)
      end
   end

   function emit:_basic(fmt, ...)
      local last = 1
      local res = {}
      for prefixpos, argpos, spec, suffixpos in string.gmatch(fmt, "()«([0-9]+):([?]*%w+)»()") do
         local prefix = string.sub(fmt, last, prefixpos - 1)
         last = suffixpos
         local arg = select(argpos, ...)
         local formatted = self:_formatsingle(arg, spec)
         table.insert(res, prefix)
         table.insert(res, formatted)
      end
      table.insert(res, string.sub(fmt, last, #fmt))
      return table.concat(res)
   end

   function emit:stmt(fmt, ...)
      table.insert(self.statments, self:_basic(fmt, ...) .. ";")
   end

   function emit:expr(fmt, ...)
      return self:_basic(fmt, ...)
   end

   function emit:opentoplevel(fmt, ...)
      table.insert(self.statments, self:_basic(fmt, ...))
   end

   function emit:closetoplevel(fmt, ...)
      table.insert(self.statments, self:_basic(fmt, ...))
   end

   function emit:include(header)
      table.insert(self.statments, self:_basic("#include «1:strlit»", header))
   end

   function emit:emittedstmts()
      return table.concat(self.statments, "\n") .. "\n"
   end

   function emit:toplevelstmt(fmt, ...)
      table.insert(self.statments, self:_basic(fmt, ...))
   end

   return emit
end

toc.opcodes = {}
toc.opschema = {}

toc.opschema.ICONST = schema "i"
function toc.opcodes.ICONST(emit, state, op)
   emit:stmt("pdcrt_op_iconst(marco, «1:int»)", op[2])
end

toc.opschema.LCONST = schema "d"
function toc.opcodes.LCONST(emit, state, op)
   local c = state.constants[op[2]]
   local t, v = c.type, c.value
   if t == "string" then
      emit:stmt("pdcrt_op_lconst_string(marco, «2:strlit»)", op[2], v)
   else
      emit:stmt("pdcrt_op_lconst_int(marco, «2:int»)", op[2], v)
   end
end

toc.opschema.SUM = schema ""
function toc.opcodes.SUM(emit, state, op)
   emit:stmt("pdcrt_op_sum(marco)")
end

toc.opschema.MUL = schema ""
function toc.opcodes.MUL(emit, state, op)
   emit:stmt("pdcrt_op_mul(marco)")
end

toc.opschema.SUB = schema ""
function toc.opcodes.SUB(emit, state, op)
   emit:stmt("pdcrt_op_sub(marco)")
end

toc.opschema.DIV = schema ""
function toc.opcodes.DIV(emit, state, op)
   emit:stmt("pdcrt_op_div(marco)")
end

toc.opschema.CALL = schema "duu"
function toc.opcodes.CALL(emit, state, op)
   emit:stmt("pdcrt_op_call(marco, PDCRT_PROC_NAME(«1:idname»), «2:int», «3:int»)", op[2], op[3], op[4])
end

toc.opschema.LGET = schema "d"
function toc.opcodes.LGET(emit, state, op)
   emit:stmt("pdcrt_op_lget(marco, PDCRT_GET_LVAR(«1:idint»))", op[2])
end

toc.opschema.LSET = schema "d"
function toc.opcodes.LSET(emit, state, op)
   emit:stmt("PDCRT_SET_LVAR(«1:idint», pdcrt_op_lset(marco))", op[2])
end

toc.opschema.RETN = schema "u"
function toc.opcodes.RETN(emit, state, op)
   emit:stmt("pdcrt_op_retn(marco, «1:int»)", op[2])
   assert(state.in_procedure, "RETN can only appear inside a procedure")
   emit:stmt("PDCRT_PROC_POSTLUDE(«1:idname»)", state.current_proc.id)
   emit:stmt("return pdcrt_real_return(marco)")
end

toc.opschema.MKENV = schema "u"
function toc.opcodes.MKENV(emit, state, op)
   emit:stmt("pdcrt_op_mkenv(marco, «1:int»)", op[2])
end

toc.opschema.ESET = schema "du"
function toc.opcodes.ESET(emit, state, op)
   emit:stmt("pdcrt_op_eset(marco, PDCRT_GET_LVAR(«1:idint»), «2:int»)", op[2], op[3])
end

toc.opschema.EGET = schema "du"
function toc.opcodes.EGET(emit, state, op)
   emit:stmt("pdcrt_op_eget(marco, PDCRT_GET_LVAR(«1:idint»), «2:int»)", op[2], op[3])
end

toc.opschema.MKCLZ = schema "d"
function toc.opcodes.MKCLZ(emit, state, op)
   emit:stmt("pdcrt_op_mkclz(marco, PDCRT_PROC_NAME(«1:idname»))", op[2])
end

toc.opschema.MKCLZC = schema "dduu"
function toc.opcodes.MKCLZC(emit, state, op)
   emit:stmt("pdcrt_op_mkclz(marco, «1:idint», PDCRT_PROC_NAME(«2:idname»), «3:int», «4:int»)", op[2], op[3], op[4], op[5])
end

toc.opschema.MK0CLZ = schema "d"
function toc.opcodes.MK0CLZ(emit, state, op)
   emit:stmt("pdcrt_op_mk0clz(marco, PDCRT_PROC_NAME(«1:idname»))", op[2])
end

toc.opschema.DYNCALL = schema "uu"
function toc.opcodes.DYNCALL(emit, state, op)
   emit:stmt("pdcrt_op_dyncall(marco, «1:int», «2:int»)", op[2], op[3])
end

toc.opschema.IFRAME = schema "d?du"
function toc.opcodes.IFRAME(emit, state, op)
   emit:stmt("pdcrt_op_iframe(marco, PDCRT_GET_LVAR(«1:idint»), «2:?idint», «3:int»)", op[2], op[3], op[4])
end

toc.opschema.EINIT = schema "dud"
function toc.opcodes.EINIT(emit, state, op)
   emit:stmt("pdcrt_op_einit(marco, PDCRT_GET_LVAR(«1:idint»), «2:int», PDCRT_GET_LVAR(«3:idint»))", op[2], op[3], op[4])
end

toc.opschema.EFRAME = schema "d"
function toc.opcodes.EFRAME(emit, state, op)
   emit:stmt("pdcrt_op_eframe(marco, PDCRT_GET_LVAR(«1:idint»))", op[2])
end

toc.opschema.PRINTT = schema ""
function toc.opcodes.PRINTT(emit, state, op)
   emit:stmt("pdcrt_op_printt(marco)")
end

toc.opschema.LSETC = schema "duu"
function toc.opcodes.LSETC(emit, state, op)
   emit:stmt("pdcrt_op_lsetc(marco, «1:idint», «2:int», «3:int»)", op[2], op[3], op[4])
end

toc.opschema.LGETC = schema "duu"
function toc.opcodes.LGETC(emit, state, op)
   emit:stmt("pdcrt_op_lgetc(marco, «1:idint», «2:int», «3:int»)", op[2], op[3], op[4])
end

toc.opschema.ESETC = schema "duu"
function toc.opcodes.ESETC(emit, state, op)
   emit:stmt("pdcrt_op_lsetc(marco, «1:idint», «2:int», «3:int»)", op[2], op[3], op[4])
end

toc.opschema.EGETC = schema "duu"
function toc.opcodes.EGETC(emit, state, op)
   emit:stmt("pdcrt_op_lgetc(marco, «1:idint», «2:int», «3:int»)", op[2], op[3], op[4])
end

function toc.opcode(emit, state, op)
   local errm = "opcode ".. op[1] .. " not implemented"
   assert(toc.opschema[op[1]], errm)(op)
   return assert(toc.opcodes[op[1]], errm)(emit, state, op)
end

function toc.compcode(emit, state)
   emit:opentoplevel("PDCRT_MAIN() {")
   emit:stmt("PDCRT_MAIN_PRELUDE(«1:int»)", #state.code.locals)
   for i = 1, #state.code.locals do
      local p = state.code.locals[i]
      emit:stmt("PDCRT_LOCAL(«1:idint», «2:idint»)", p[2], p[2])
   end
   for i = 1, #state.code.opcodes do
      toc.opcode(emit, state, state.code.opcodes[i])
   end
   emit:stmt("PDCRT_MAIN_POSTLUDE()")
   emit:closetoplevel("}")
end

function toc.opproc(emit, state, proc)
   emit:opentoplevel("PDCRT_PROC(«1:idname») {", proc.id)
   emit:stmt("PDCRT_PROC_PRELUDE(«1:idname», «2:int»)", proc.id, #proc.params + #proc.locals)
   emit:stmt("PDCRT_ASSERT_PARAMS(«1:int»)", #proc.params)
   for i = 1, #proc.params do
      local p = proc.params[i]
      emit:stmt("PDCRT_PARAM(«2:idint», «2:idname»)", p[2], p[2])
   end
   for i = 1, #proc.locals do
      local p = proc.locals[i]
      emit:stmt("PDCRT_LOCAL(«2:idint», «2:idname»)", p[2], p[2])
   end
   for i = 1, #proc.opcodes do
      toc.opcode(emit, state, proc.opcodes[i])
   end
   emit:stmt("PDCRT_PROC_POSTLUDE(«1:idname»)", proc.id)
   emit:stmt("return pdcrt_passthru_return(marco)")
   emit:closetoplevel("}")
end

function toc.compprocs(emit, state)
   for id, proc in pairs(state.procedures) do
      local extra = {
         in_procedure = true,
         current_proc = proc,
      }
      toc.opproc(emit, attach_extra(state, extra), proc)
   end
end

function toc.compprocdeclrs(emit, state)
   for id, proc in pairs(state.procedures) do
      emit:toplevelstmt("PDCRT_DECLARE_PROC(«1:idname»)", id)
   end
end


local function checklocals(tp, body)
   local base = 0
   if tp == "proc" then
      for i = 1, #body.params do
         if body.params[i][2] ~= (i - 1) then
            warnabout("increasing_locals", "non-increasing parameter ID")
         end
      end
      base = #body.params
   end
   for i = 1, #body.locals do
      if body.locals[i][2] ~= (i + base - 1) then
         warnabout("increasing_locals", "non-increasing local ID")
      end
   end
end

local function main(input, config)
   local r = re.compile(grammar)
   log.info("compiling the file")
   local secs = sectionstotable(assert(re.match(input, r), "could not parse bytecode"))
   log.info("done compiling")
   assert(tonumber(secs.version[1]) == VER[1], "major version must be 1")
   assert(tonumber(secs.version[2]) <= VER[2], "minor version must be 0 or less")
   log.info("validated the version")
   if secs.procedures_section then
      local P = {}
      for i = 1, #secs.procedures_section do
         local r = prepproc(secs.procedures_section[i])
         if P[r.id] ~= nil then
            warnabout("redefined_procedure", "duplicate procedure with id #%d", r.id)
         end
         P[r.id] = r
      end
      secs.procedures_section = P
   else
      warnabout("no_procedure_section", "no procedure section")
   end
   log.info("extracted procedures")
   local code = processcode(codetotable(assert(secs.code_section, "code section not provided")))
   log.info("processed code")
   local state = {
      version = secs.version,
      procedures = secs.procedures_section,
      constants = processconstpool(secs.constant_pool_section),
      code = code,
   }
   checklocals("code", state.code)
   for id, body in pairs(state.procedures) do
      checklocals("proc", body)
   end
   log.info("generated state")
   local emit = toc.makeemitter()
   emit:include("pdcrt.h")
   log.dbg("emitted prelude")
   toc.compprocdeclrs(emit, state)
   log.dbg("emitted proc. declrs.")
   toc.compcode(emit, state)
   log.dbg("emitted code")
   toc.compprocs(emit, state)
   log.dbg("emitted procs.")
   log.info("assembled everything")
   return emit:emittedstmts()
end


local function readall(filename)
   local handle <close> = io.open(filename, "r")
   return handle:read("a")
end

local function writeto(filename, contents)
   local handle <close> = io.open(filename, "wb")
   handle:write(contents)
end


local sample = [=[

PDVM 1.0
PLATFORM "pdcrt"

SECTION "code"
  LOCAL 0
  LOCAL 1
  LOCAL 2
  IFRAME 1, NIL, 2
  ICONST 0
  LSET 2
  EINIT 1, 0, 2
  ICONST 1
  LSET 2
  EINIT 1, 1, 2
  EFRAME 1
  MKCLZ 1, 3, 0
  LGET 0
  DYNCALL 0, 1
  PRINTT
ENDSECTION

SECTION "procedures"
  PROC 6
    PARAM ESUP
    PARAM 0
    IFRAME EACT, ESUP, 0
    EFRAME EACT
    LGET 0
    LSETC EACT, 1, 1
    LGET 0
    LSETC EACT, 2, 0
    EGETC EACT, 1, 2
    DYNCALL 0, 1
    PRINTT
  ENDPROC

  PROC 3
    PARAM ESUP
    PARAM 0
    PARAM 1
    LOCAL 2
    IFRAME EACT, ESUP, 3
    EINIT EACT, 1, 1
    EINIT EACT, 2, 2
    EFRAME EACT
    MKCLZC EACT, 6, 0, 2
    LGET 0
    LSETC EACT, 1, 0
    EGETC EACT, 0, 2
    DYNCALL 0, 1
    PRINTT
  ENDPROC
ENDSECTION

SECTION "constant pool"
  #0 STRING "module"
ENDSECTION

]=]

_ = [=[
#0	proc	6
		#0	0
	#0		[12] PARAM	ESUP
	#1		[12] PARAM	0
	#2		[29] IFRAME	EACT	ESUP
	#3		[30] EFRAME	EACT
	#4		[5] LGET	0
	#5		[6] LSETC	EACT	1	1
	#6		[5] LGET	0
	#7		[6] LSETC	EACT	2	0
	#8		[21] EGETC	EACT	1	2
	#9		[28] DYNCALL	0	1
	#10		[16] PRINTT
	end
#1	proc	3
		#0	0
		#1	1
	#0		[12] PARAM	ESUP
	#1		[12] PARAM	0
	#2		[12] PARAM	1
	#3		[11] LOCAL	2
	#4		[29] IFRAME	EACT	ESUP
	#5		[23] EINIT	EACT	1	1
	#6		[23] EINIT	EACT	2	2
	#7		[30] EFRAME	EACT
	#8		[18] MKCLZC	EACT	6	0	2
	#9		[5] LGET	0
	#10		[6] LSETC	EACT	1	0
	#11		[21] EGETC	EACT	0	2
	#12		[28] DYNCALL	0	1
	#13		[16] PRINTT
	end
]=]

local function makeparsecli(opts)
   local function parser(cli)
      local i = 1
      local r = {
         OPTS = opts,
      }
      while i <= #cli do
         local a = cli[i]
         if a == "-" or a:sub(1, 1) ~= "-" then
            break
         end
         local ended = false
         for j = 2, #a do
            if a:sub(j, j) == "-" then
               ended = true
            else
               for n = 1, #opts do
                  local o = opts[n]
                  if a:sub(j, j) == o[1] then
                     r[o[2]] = {}
                     table.move(cli, i + 1, i + o[3], 1, r[o[2]])
                     if o[3] == 0 then
                        r[o[2]] = true
                     elseif o[3] == 1 then
                        r[o[2]] = r[o[2]][1]
                     end
                     i = i + o[3]
                     break
                  end
               end
            end
         end
         i = i + 1
         if ended then
            break
         end
      end
      table.move(cli, i, #cli, 1, r)
      return r
   end
   return parser
end

local parser = makeparsecli {
   {"o", "output", 1, "Archivo en el cual guardar la salida."},
   {"h", "help", 0, "Muestra esta ayuda y termina."},
   {"v", "version", 0, "Muestra la versión del ensamblador"},
   {"V", "verbose", 0, "Muestra salida adicional."},
   {"s", "sample", 0, "Compila el programa de prueba."},
   {"W", "warning", 1, "Activa la advertencia especificada."},
}
local res = parser {...}

if res.help then
   print(([[Ensamblador de la máquina virtual de PseudoD.

Uso: lua5.4 main.lua [opts...] archivos...

Este programa solo acepta opciones cortas (como `-h` o `-v`), las cuales
tienen que estar antes de los argumentos. Las opciones se pueden combinar.
Puedes separar las opciones de los argumentos con `--`.
]]):format())
   print((" % 8s  % 15s  %s"):format("Opción", "Núm. argumentos", "Descripción"))
   for i = 1, #res.OPTS do
      local opt = res.OPTS[i]
      print(("% 8s  % 15d  %s"):format("-"..opt[1], opt[3], opt[4]))
   end
   os.exit(true, true)
end

if res.version then
   print(("%d.%d.%d"):format(VER[1], VER[2], VER[3]))
   os.exit(true, true)
end

if res.verbose then
   log.min = 0
end

if res.warning then
   local s = {}
   for w in string.gmatch(res.warning, "(%w+)") do
      for i = 1, #WARNINGS do
         local W = WARNINGS[i]
         if W[1] == w or W[2] == w then
            for j = 1, #W[3] do
               enabledwarnings[W[3][j]] = true
            end
         end
      end
   end
end

local config = {}
local compiled
if res.sample then
   compiled = main(sample, config)
else
   assert(type(res[1]) == "string", "se esperaba un archivo de entrada")
   compiled = main(readall(res[1]), config)
end
if res.output then
   writeto(res.output, compiled)
else
   print(compiled)
end
