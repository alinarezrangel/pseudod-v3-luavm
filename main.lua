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
   -- {nombre-largo, nombre-corto, {llaves-en-enabledwarnings...}, ayuda}
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
   {"all", "all", {--[[ el código más adelante llena este campo ]]},
    "Activa todas las advertencias."},
}

do
   local alli, allnames = 0, {}
   for i = 1, #WARNINGS do
      local W = WARNINGS[i]
      if W[1] == "all" then
         alli = i
      else
         for j = 1, #W[3] do
            table.insert(allnames, W[3][j])
         end
      end
   end
   assert(alli > 0, "could not find index of <all> warning")
   WARNINGS[alli][3] = allnames
end

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

OP <- "LCONST" / "ICONST" / "FCONST" / "SUM" / "SUB"
    / "MUL" / "DIV" / "RETN" / "DYNCALL"
    / "MKCLZ" / "MK0CLZ"
    / "OPNFRM" / "EINIT" / "CLSFRM"
    / "LSETC" / "LGETC" / "LSET" / "LGET"
    / "POP" / "CHOOSE" / "JMP" / "NAME"
    / "MTRUE" / "CMPEQ" / "CMPNEQ" / "NOT"
    / "ROT" / "GT" / "LT" / "GE" / "LE"
    / "MSG"
    / "PRN" / "NL"

procsec <- {| '' -> 'procedures_section'
              "SECTION" ws '"procedures"' (rs proc)* rs "ENDSECTION" |}
proc <- {| '' -> 'proc'
           "PROC" rs id {| '' -> 'params'  (rs param)* |} code rs "ENDPROC" |}
param <- {| {"PARAM"} rs (id / envs) |}

unknownsec <- "SECTION" ws str (ws token)* rs "SECTION"
token <- str / [^%s"]+

-- Grammar end.

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

local schemagrammar = [=[

schema <- {| (ws arg (ws ',' ws arg)*)? ws !. |}
s <- %s
ws <- s*
arg <- {| {mod?} {type} {name?} |}
mod <- '?'
type <- [LPFCTEUI]
name <- [a-z][a-z0-9]*

]=]

local schemare = re.compile(schemagrammar)

local function schema(s)
   local ss = assert(re.match(s, schemare), ("could not parse schema %q"):format(s))
   return function(op)
      assert(#op - 1 == #ss, "unexpected number of arguments to opcode " .. op[1])
      for i = 1, #ss do
         local sel = ss[i]
         local oel = op[i + 1]
         if sel[1] ~= "?" or oel ~= nil then
            if sel[2] == "L" then
               assert(math.type(oel) == "integer" and oel >= 0, "expected local index")
            elseif sel[2] == "E" then
               assert((type(oel) == "table" and (oel.type == "ESUP" or oel.type == "EACT")) or (math.type(oel) == "integer" and oel >= 0), "expected env. local index")
            elseif sel[2] == "U" then
               assert(math.type(oel) == "integer" and oel >= 0, "expected non-negative local")
            elseif sel[2] == "P" then
               assert(math.type(oel) == "integer" and oel >= 0, "expected procedure index")
            elseif sel[2] == "C" then
               assert(math.type(oel) == "integer" and oel >= 0, "expected constant index")
            elseif sel[2] == "T" then
               assert(math.type(oel) == "integer" and oel >= 0, "expected target index")
            elseif sel[2] == "I" then
               assert(math.type(oel) == "integer", "expected integer")
            elseif sel[2] == "F" then
               assert(math.type(oel) == "float", "expected float")
            else
               error("unreachable")
            end
         end
         if sel[3] then
            op[sel[2] .. sel[3]] = oel
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
      elseif spec == "localname" then
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
      elseif spec == "localid" then
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
      elseif spec == "procid" then
         assert(math.type(arg) == "integer" and arg >= 0, "expected integer")
         return tostring(arg)
      elseif spec == "procname" then
         assert(math.type(arg) == "integer" and arg >= 0, "expected integer")
         return ("PDCRT_PROC_NAME(name_%s)"):format(tostring(arg))
      elseif spec == "labelid" then
         assert(math.type(arg) == "integer" and arg >= 0, "expected label")
         return tostring(arg)
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

   function emit:comment(comm)
      table.insert(self.statments, self:_basic("/* «1:strlit» */", comm))
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

toc.opschema.ICONST = schema "Ix"
function toc.opcodes.ICONST(emit, state, op)
   emit:stmt("pdcrt_op_iconst(marco, «1:int»)", op.Ix)
end

toc.opschema.LCONST = schema "Cx"
function toc.opcodes.LCONST(emit, state, op)
   local c = state.constants[op[2]]
   local t, v = c.type, c.value
   if t == "string" then
      emit:stmt("pdcrt_op_lconst(marco, «1:int»)", op.Cx)
   else
      emit:stmt("pdcrt_op_lconst_int(marco, «2:int»)", op.Cx, v)
   end
end

toc.opschema.PRN = schema ""
function toc.opcodes.PRN(emit, state, op)
   emit:stmt("pdcrt_op_prn(marco)")
end

toc.opschema.NL = schema ""
function toc.opcodes.NL(emit, state, op)
   emit:stmt("pdcrt_op_nl(marco)")
end

toc.opschema.POP = schema ""
function toc.opcodes.POP(emit, state, op)
   emit:stmt("pdcrt_op_pop(marco)")
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

toc.opschema.GT = schema ""
function toc.opcodes.GT(emit, state, op)
   emit:stmt("pdcrt_op_gt(marco)")
end

toc.opschema.LT = schema ""
function toc.opcodes.LT(emit, state, op)
   emit:stmt("pdcrt_op_lt(marco)")
end

toc.opschema.GE = schema ""
function toc.opcodes.GE(emit, state, op)
   emit:stmt("pdcrt_op_ge(marco)")
end

toc.opschema.LE = schema ""
function toc.opcodes.LE(emit, state, op)
   emit:stmt("pdcrt_op_le(marco)")
end

toc.opschema.LSET = schema "Lx"
function toc.opcodes.LSET(emit, state, op)
   emit:stmt("PDCRT_SET_LVAR(«1:localid», pdcrt_op_lset(marco))", op.Lx)
end

toc.opschema.LGET = schema "Lx"
function toc.opcodes.LGET(emit, state, op)
   emit:stmt("pdcrt_op_lget(marco, PDCRT_GET_LVAR(«1:localid»))", op.Lx)
end

toc.opschema.LSETC = schema "Ex, Ua, Ui"
function toc.opcodes.LSETC(emit, state, op)
   emit:stmt("pdcrt_op_lsetc(marco, PDCRT_GET_LVAR(«1:localid»), «2:int», «3:int»)", op.Ex, op.Ua, op.Ui)
end

toc.opschema.LGETC = schema "Ex, Ua, Ui"
function toc.opcodes.LGETC(emit, state, op)
   emit:stmt("pdcrt_op_lgetc(marco, PDCRT_GET_LVAR(«1:localid»), «2:int», «3:int»)", op.Ex, op.Ua, op.Ui)
end

toc.opschema.OPNFRM = schema "Ex, ?Ey, Ux"
function toc.opcodes.OPNFRM(emit, state, op)
   emit:stmt("PDCRT_SET_LVAR(«1:localid», pdcrt_op_open_frame(marco, «2:?localid», «3:int»))", op.Ex, op.Ey, op.Ux)
end

toc.opschema.EINIT = schema "Ex, Ui, Lx"
function toc.opcodes.EINIT(emit, state, op)
   emit:stmt("pdcrt_op_einit(marco, PDCRT_GET_LVAR(«1:localid»), «2:int», PDCRT_GET_LVAR(«3:localid»))", op.Ex, op.Ui, op.Lx)
end

toc.opschema.CLSFRM = schema "Ex"
function toc.opcodes.CLSFRM(emit, state, op)
   emit:stmt("pdcrt_op_close_frame(marco, PDCRT_GET_LVAR(«1:localid»))", op.Ex)
end

toc.opschema.MKCLZ = schema "Ex, Px"
function toc.opcodes.MKCLZ(emit, state, op)
   emit:stmt("pdcrt_op_mkclz(marco, «1:localid», «2:procname»)", op.Ex, op.Px)
end

toc.opschema.MK0CLZ = schema "Px"
function toc.opcodes.MK0CLZ(emit, state, op)
   emit:stmt("pdcrt_op_mk0clz(marco, «1:procname»)", op.Px)
end

toc.opschema.DYNCALL = schema "Ux, Uy"
function toc.opcodes.DYNCALL(emit, state, op)
   emit:stmt("pdcrt_op_dyncall(marco, «1:int», «2:int»)", op.Ux, op.Uy)
end

toc.opschema.CHOOSE = schema "Tx, Ty"
function toc.opcodes.CHOOSE(emit, state, op)
   emit:stmt("if(pdcrt_op_choose(marco)) { goto PDCRT_LABEL(«1:labelid»); } else { goto PDCRT_LABEL(«2:labelid»); }", op.Tx, op.Ty)
end

toc.opschema.NAME = schema "Tx"
function toc.opcodes.NAME(emit, state, op)
   emit:stmt("PDCRT_LABEL(«1:labelid»):", op.Tx)
end

toc.opschema.JMP = schema "Tx"
function toc.opcodes.JMP(emit, state, op)
   emit:stmt("goto PDCRT_LABEL(«1:labelid»)", op.Tx)
end

toc.opschema.ROT = schema "Ia"
function toc.opcodes.ROT(emit, state, op)
   emit:stmt("pdcrt_op_rot(marco, «1:int»)", op.Ia)
end

toc.opschema.RETN = schema "Ua"
function toc.opcodes.RETN(emit, state, op)
   emit:stmt("pdcrt_op_retn(marco, «1:int»)", op.Ua)
   emit:stmt("PDCRT_RETURN()")
end

toc.opschema.NOT = schema ""
function toc.opcodes.NOT(emit, state, op)
   emit:stmt("pdcrt_op_not(marco)")
end

toc.opschema.MTRUE = schema ""
function toc.opcodes.MTRUE(emit, state, op)
   emit:stmt("pdcrt_op_mtrue(marco)")
end

toc.opschema.CMPEQ = schema ""
function toc.opcodes.CMPEQ(emit, state, op)
   emit:stmt("pdcrt_op_cmp(marco, PDCRT_CMP_EQ)")
end

toc.opschema.CMPNEQ = schema ""
function toc.opcodes.CMPNEQ(emit, state, op)
   emit:stmt("pdcrt_op_cmp(marco, PDCRT_CMP_NEQ)")
end

toc.opschema.MSG = schema "Cx, Ua, Ub"
function toc.opcodes.MSG(emit, state, op)
   emit:stmt("pdcrt_op_msg(marco, «1:int», «2:int», «3:int»)", op.Cx, op.Ua, op.Ub)
end

-- Opcodes end.

function toc.opcode(emit, state, op)
   local errm = "opcode ".. op[1] .. " not implemented"
   assert(toc.opschema[op[1]], errm)(op)
   return assert(toc.opcodes[op[1]], errm)(emit, state, op)
end

function toc.compconsts(emit, state)
   for i, c in pairs(state.constants) do
      if c.type == "string" then
         emit:stmt("PDCRT_REGISTRAR_TXTLIT(«1:int», «2:strlit»)", i, c.value)
      else
         error("not implemented constant type " .. c.type)
      end
   end
end

function toc.compcode(emit, state)
   emit:opentoplevel("PDCRT_MAIN() {")
   emit:stmt("PDCRT_MAIN_PRELUDE(«1:int»)", #state.code.locals)
   log.dbg("starting to emit consts inside code")
   toc.compconsts(emit, state)
   log.dbg("emitted consts.")
   for i = 1, #state.code.locals do
      local p = state.code.locals[i]
      emit:stmt("PDCRT_LOCAL(«1:localid», «1:localname»)", p[2])
   end
   for i = 1, #state.code.opcodes do
      toc.opcode(emit, state, state.code.opcodes[i])
   end
   emit:stmt("PDCRT_MAIN_POSTLUDE()")
   emit:closetoplevel("}")
end

function toc.opproc(emit, state, proc)
   emit:opentoplevel("PDCRT_PROC(«1:localname») {", proc.id)
   emit:stmt("PDCRT_PROC_PRELUDE(«1:localname», «2:int»)", proc.id, #proc.params + #proc.locals)
   emit:stmt("PDCRT_ASSERT_PARAMS(«1:int»)", #proc.params)
   for i = 1, #proc.params do
      local p = proc.params[i]
      emit:stmt("PDCRT_PARAM(«1:localid», «1:localname»)", p[2])
   end
   for i = 1, #proc.locals do
      local p = proc.locals[i]
      emit:stmt("PDCRT_LOCAL(«1:localid», «1:localname»)", p[2])
   end
   for i = 1, #proc.opcodes do
      toc.opcode(emit, state, proc.opcodes[i])
   end
   emit:stmt("PDCRT_PROC_POSTLUDE(«1:localname»)", proc.id)
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
      emit:toplevelstmt("PDCRT_DECLARE_PROC(«1:localname»)", id)
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
  LCONST 1
  MSG 2, 0, 1
  PRN
  NL
ENDSECTION

SECTION "procedures"
  PROC 0
    LOCAL 0
    OPNFRM EACT, NIL, 1
    EINIT EACT, 0, 0
    CLSFRM EACT
    MKCLZ EACT, 1
    LSETC EACT, 0, 0
    LGETC EACT, 0, 0
    ICONST 20
    ROT 1
    DYNCALL 1, 1
    ROT 0
    MSG 0, 0, 1
    PRN
    NL
  ENDPROC

  PROC 1
    PARAM ESUP
    PARAM 0
    OPNFRM EACT, ESUP, 0
    CLSFRM EACT
    LGET 0
    ICONST 2
    LT
    CHOOSE 1, 2
    NAME 1
    ICONST 1
    RETN 1
    JMP 3
    NAME 2
    LGETC EACT, 1, 0
    LGET 0
    ICONST 1
    SUB
    ROT 1
    DYNCALL 1, 1
    LGETC EACT, 1, 0
    LGET 0
    ICONST 2
    SUB
    ROT 1
    DYNCALL 1, 1
    SUM
    RETN 1
    NAME 3
  ENDPROC
ENDSECTION

SECTION "constant pool"
  #0 STRING "comoTexto"
  #1 STRING "6"
  #2 STRING "comoNumeroEntero"
ENDSECTION

]=]

-- Sample end.

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
   {"O", "stdout", 0, "Escribe el compilado a la salida estándar."},
   {"h", "help", 0, "Muestra esta ayuda y termina."},
   {"v", "version", 0, "Muestra la versión del ensamblador"},
   {"V", "verbose", 0, "Muestra salida adicional."},
   {"s", "sample", 0, "Compila el programa de prueba."},
   {"W", "warning", 1, "Activa las advertencias especificadas (separadas por comas)."},
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
   print(("\nAdvertencias:\n\n %- 27s  %- 17s"):format("Nom. largo", "Nom. corto"))
   for i = 1, #WARNINGS do
      local W = WARNINGS[i]
      print((" %- 27s  %- 17s"):format(W[1], W[2]))
      print("    " .. W[4])
      if i ~= #WARNINGS then
         print()
      end
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
if not res.stdout and res.output then
   writeto(res.output, compiled)
else
   print(compiled)
end
