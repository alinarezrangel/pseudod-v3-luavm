local re = require "re"

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

   {"redefined-procedure", "redef-proc", {"redefined_procedure"},
    "Advierte cuando se redefina un procedimiento."},

   {"no-procedure-section", "no-proc-sec", {"no_procedure_section"},
    "Advierte si no hay sección de procedimientos."},

   {"no-constant-pool", "no-const-pool", {"no_constant_pool"},
    "Advierte si no hay una sección de \"lista de constantes\"."},

   {"empty-function", "empty-fn", {"empty_function"},
    "Advierte si se encuentra una función vacía."},

   {"no-modules", "no-mods", {"no_modules"},
    "Advierte si se declaró ningún módulo."},

   {"out-of-range-constant", "oor-const", {"oor_const"},
    "Advierte si el ID de alguna constante está fuera del rango válido."},

   {"future", "future", {"future"},
    "Advierte sobre cosas que van a cambiar en un futuro."},

   {"useful", "useful", {"redefined_constant", "redefined_procedure",
                         "no_procedure_section", "no_constant_pool",
                         "empty_function", "future", "oor_const"},
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
comment <- ";" [^%nl]* / "--" [^%nl]*

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
opcode <- {| {:srcpos: {} :} {OP} ![a-zA-Z0-9_] (rs oparg (ws "," ws oparg)*)? |}
oparg <- nil / envs / flt / int / str
nil <- {| {:type: '' -> 'nil' :} 'NIL' |}
envs <- {| {:type: '' -> 'env' :} { 'ESUP' / 'EACT' } |}

code <- {| '' -> 'locals'  (rs local)* |}
        {| '' -> 'opcodes'  (rs opcode)* |}
local <- {| {"LOCAL"} rs (id / envs) |}

OP <- "LCONST" / "ICONST" / "FCONST" / "BCONST"
    / "SUM" / "SUB" / "MUL" / "DIV"
    / "RETN" / "DYNCALL"
    / "MKCLZ" / "MK0CLZ" / "MKARR"
    / "OPNFRM" / "EINIT" / "ENEW" / "CLSFRM"
    / "LSETC" / "LGETC" / "LSET" / "LGET"
    / "POP" / "CHOOSE" / "JMP" / "NAME"
    / "MTRUE" / "CMPEQ" / "CMPNEQ" / "CMPREFEQ" / "NOT"
    / "ROT" / "GT" / "LT" / "GE" / "LE" / "OPEQ"
    / "CLZ2OBJ" / "OBJ2CLZ"
    / "TMSG" / "MSG"
    / "PRN" / "NL"
    / "OPNEXP" / "CLSEXP" / "EXP" / "IMPORT" / "SAVEIMPORT" / "MODULE"
    / "SPUSH" / "SPOP"

procsec <- {| '' -> 'procedures_section'
              "SECTION" ws '"procedures"' (rs proc)* rs "ENDSECTION" |}
proc <- {| '' -> 'proc'
           "PROC" rs id
           {| ('' -> 'method' rs 'METHOD' / '' -> 'no_method') |}
           {| '' -> 'params' (rs param)* |}
           {| ('' -> 'variadic' rs variadic  / '' -> 'no_variadic') |}
           code rs "ENDPROC" |}
param <- {| {"PARAM"} rs (id / envs) |}
variadic <- "VARIADIC" rs id

unknownsec <- "SECTION" ws str (ws token)* rs "ENDSECTION"
token <- str / [^%s"]+

-- Grammar end.

]==]

local function pv(...)
   require "fennel"
   local v = require "fennel.view"
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
         return ("\"\"\\x%02x\"\""):format(string.byte(st))
      end
   end
   return (string.gsub(str, "[^a-zA-Z0-9%.%+/%^&%$@ %-%*_%%:;,]", repl))
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
   r.srcpos = tbl.srcpos
   return r
end

local function binary_search(seq, el, comp)
   comp = comp or function(a, b)
      if a < b then
         return -1
      elseif a > b then
         return 1
      else
         return 0
      end
   end

   local function search(from, to)
      if to < from then
         return nil, from, to
      elseif from == to then
         if comp(seq[from], el) == 0 then
            return from
         else
            return nil, from, to
         end
      else
         local middle = math.floor((to - from) / 2) + from
         local oel = seq[middle]
         local c = comp(el, oel)
         if c == 0 then
            return middle
         elseif c < 0 then
            return search(from, middle - 1)
         else
            return search(middle + 1, to)
         end
      end
   end
   return search(1, #seq)
end


local function srcposes_to_srclocs(src, srcposes)
   if #srcposes == 0 then
      return {}
   end

   local sorted = {}
   for i = 1, #srcposes do
      sorted[i] = srcposes[i]
   end
   table.sort(sorted)
   local unsorted = {}
   for i = 1, #srcposes do
      local srcpos = srcposes[i]
      local idx = binary_search(sorted, srcpos)
      assert(idx)
      unsorted[idx] = i
   end

   local lineno, colno = 1, 0
   local last_srcpos, sidx = sorted[#sorted], 1
   local srclocs = {}
   for i = 1, last_srcpos do
      if sorted[sidx] == i then
         srclocs[#srclocs + 1] = { lineno = lineno, colno = colno, byteno = i }
         sidx = sidx + 1
      end

      local char = string.sub(src, i, i)
      if char == "\n" then
         lineno = lineno + 1
         colno = 0
      else
         colno = colno + 1
      end
   end

   return srclocs
end


local function processcode(code)
   local res = { locals = {}, opcodes = {}, srcposes = {} }
   for i = 1, #code.locals do
      res.locals[i] = opcode(code.locals[i])
   end
   for i = 1, #code.opcodes do
      res.srcposes[i] = code.opcodes[i].srcpos
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
   c.method = proc[3][1] == 'method'
   c.params = sliceseq(proc[4], 2, #proc[4])
   for i = 1, #c.params do
      c.params[i] = opcode(c.params[i])
   end
   if proc[5][1] == "variadic" then
      c.variadic = processoparg(proc[5][2])
   else
      c.variadic = nil
   end
   c.locals = sliceseq(proc[6], 2, #proc[6])
   for i = 1, #c.locals do
      c.locals[i] = opcode(c.locals[i])
   end
   c.opcodes = sliceseq(proc[7], 2, #proc[7])
   c.srcposes = {}
   for i = 1, #c.opcodes do
      c.srcposes[i] = c.opcodes[i].srcpos
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
      if id < 0 or id > (#cpool - 1) then
         warnabout("oor_const", "constant with ID %d is out of range", id)
      end
      pool[id] = {
         type = c[3],
         value = processoparg(c[4]),
      }
   end
   return pool
end


local REQUIRES_CONTINUATION = {
   "LT", "GT", "LE", "GE", "OPEQ", -- Llamadas a función (operadores)
   "CMPEQ", "CMPNEQ", "CMPREFEQ",  -- Llamadas a función (método `igualA`)
   "TMSG", "MSG", "DYNCALL",       -- Llamadas a función
   "SUM", "SUB", "MUL", "DIV",     -- Llamadas a función (operadores)
   "IMPORT",                       -- Importar un módulo (control de flujo)
   "NAME", "JMP", "CHOOSE"         -- Control de flujo
}

local FALLS_THROUGHT = {
   "NAME",
}

local function contains(tbl, el)
   for i = 1, #tbl do
      if tbl[i] == el then
         return true
      end
   end
   return false
end

local function splitcode(code)
   local parts, pi = {{}}, 1
   for i = 1, #code.opcodes do
      local instr = code.opcodes[i]
      local opcode = instr[1]
      local lastpart = parts[#parts]
      if contains(REQUIRES_CONTINUATION, opcode) then
         lastpart.kreq = instr
         lastpart.srcposes = sliceseq(code.srcposes, pi, i)
         pi = i + 1
         if i ~= #code.opcodes then
            parts[#parts + 1] = {}
         end
      else
         if i == #code.opcodes then
            lastpart.srcposes = sliceseq(code.srcposes, pi, i)
         end
         lastpart[#lastpart + 1] = instr
      end
   end

   local res = {}
   for k, v in pairs(code) do
      res[k] = v
   end
   res.opcodes = nil
   res.parts = parts
   return res
end


local function attach_extra(tbl, extra)
   local function index(t, k)
      local v = extra[k]
      if v ~= nil then
         return v
      else
         return tbl[k]
      end
   end
   local r = {}
   return setmetatable(r, {__index = index, tbl = tbl, extra = extra})
end

local function extra_wrapped(tbl)
   return getmetatable(tbl).tbl
end

local function extra_extra(tbl)
   return getmetatable(tbl).extra
end


local toc = {}

local schemagrammar = [=[

schema <- {| (ws arg (ws ',' ws arg)*)? ws !. |}
s <- %s
ws <- s*
arg <- {| {mod?} {type} {name?} |}
mod <- '?'
type <- [LPFCTEUIB]
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
            elseif sel[2] == "B" then
               assert(math.type(oel) == "integer" and (oel == 0 or oel == 1), "expected bool (1 or 0)")
               oel = oel == 1
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

local function autoescape_raw(code)
   return { raw = code }
end

local function autoescape_to_c(value)
   if type(value) == "number" then
      -- Véase la advertencia de `_formatsingle()` más abajo.
      return tostring(value)
   elseif type(value) == "boolean" then
      -- No es necesario usar las macros TRUE y FALSE ya que pdcrt.h incluye
      -- `<stdbool.h>`
      if value then
         return "true"
      else
         return "false"
      end
   elseif type(value) == "string" then
      return escapecstr(value)
   elseif type(value) == "nil" then
      return "NULL"
   elseif type(value) == "table" and type(value.raw) == "string" then
      return value.raw
   else
      error("could not automatically escape value to C: " .. tostring(value))
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
      if spec:sub(1, 1) == "*" then
         spec = spec:sub(2)
         local ornil = false
         if optional then
            ornil = arg == nil
         end
         assert(type(arg) == "table" or ornil, "expected table for array")
         if optional and arg == nil then
            return "NULL"
         else
            local parts = {}
            for i = 1, #arg do
               parts[i] = self:_formatsingle(arg[i], spec)
            end
            return ("{%s}"):format(table.concat(parts, ", "))
         end
      end

      if spec == "int" then
         assert(not optional and math.type(arg) == "integer", "expected integer")
         -- Emite el argumento como un entero de C. `tostring()` no es correcto
         -- (lo convierte a un entero de Lua) pero debería funcionar por ahora.
         return tostring(arg)
      elseif spec == "bool" then
         assert(not optional and type(arg) == "boolean")
         if arg then
            return "true"
         else
            return "false"
         end
      elseif spec == "flt" then
         assert(not optional and math.type(arg) == "float", "expected float")
         -- Lo mismo que con `int`.
         return tostring(arg)
      elseif spec == "localname" then
         assert(not optional, "«localname» cannot be optional")
         local isspecial = false
         if arg == ESUP or arg == EACT then
            isspecial = true
         end
         assert(isspecial or (math.type(arg) == "integer" and arg >= 0), "expected local id")
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
         assert(ornil or isspecial or (math.type(arg) == "integer" and arg >= 0), "expected local id")
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
         assert(not optional and math.type(arg) == "integer" and arg >= 0, "expected procedure id")
         return tostring(arg)
      elseif spec == "procname" then
         assert(not optional and math.type(arg) == "integer" and arg >= 0, "expected procedure id")
         return ("PDCRT_PROC_NAME(name_%s)"):format(tostring(arg))
      elseif spec == "labelid" then
         assert(not optional and math.type(arg) == "integer" and arg >= 0, "expected label")
         return tostring(arg)
      elseif spec == "contproc" then
         assert(not optional and math.type(arg) == "integer" and arg >= 0, "expected continuation procedure")
         return "name_" .. tostring(arg)
      elseif spec == "contname" then
         assert(not optional and math.type(arg) == "integer" and arg >= 0, "expected continuation id")
         return "k" .. tostring(arg)
      elseif spec == "strlit" then
         assert(not optional and type(arg) == "string", "expected string")
         return '"' .. escapecstr(arg) .. '"'
      elseif spec == "structlit" then
         assert(not optional and type(arg) == "table", "expected table for «structlit»")
         local fields = {}
         for name, value in pairs(arg) do
            fields[#fields + 1] = (".%s = %s"):format(name, autoescape_to_c(value))
         end
         return ("{%s}"):format(table.concat(fields, ", "))
      else
         error("unknown specifier " .. spec)
      end
   end

   function emit:_basic(fmt, ...)
      local last = 1
      local res = {}
      for prefixpos, argpos, spec, suffixpos in string.gmatch(fmt, "()«([0-9]+):([?%*]*%w+)»()") do
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

toc.opschema.BCONST = schema "Bx"
function toc.opcodes.BCONST(emit, state, op)
   emit:stmt("pdcrt_op_bconst(marco, «1:bool»)", op.Bx)
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

toc.opschema.FCONST = schema "Fx"
function toc.opcodes.FCONST(emit, state, op)
   emit:stmt("pdcrt_op_fconst(marco, «1:flt»)", op.Fx)
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
   emit:stmt("return pdcrt_op_sum(marco, PDCRT_CONT_NAME(«1:contproc», «2:contname»))",
             state.current_proc.id, state.next_ccid)
end

toc.opschema.MUL = schema ""
function toc.opcodes.MUL(emit, state, op)
   emit:stmt("return pdcrt_op_mul(marco, PDCRT_CONT_NAME(«1:contproc», «2:contname»))",
             state.current_proc.id, state.next_ccid)
end

toc.opschema.SUB = schema ""
function toc.opcodes.SUB(emit, state, op)
   emit:stmt("return pdcrt_op_sub(marco, PDCRT_CONT_NAME(«1:contproc», «2:contname»))",
             state.current_proc.id, state.next_ccid)
end

toc.opschema.DIV = schema ""
function toc.opcodes.DIV(emit, state, op)
   emit:stmt("return pdcrt_op_div(marco, PDCRT_CONT_NAME(«1:contproc», «2:contname»))",
             state.current_proc.id, state.next_ccid)
end

toc.opschema.GT = schema ""
function toc.opcodes.GT(emit, state, op)
   emit:stmt("return pdcrt_op_gt(marco, PDCRT_CONT_NAME(«1:contproc», «2:contname»))",
             state.current_proc.id, state.next_ccid)
end

toc.opschema.LT = schema ""
function toc.opcodes.LT(emit, state, op)
   emit:stmt("return pdcrt_op_lt(marco, PDCRT_CONT_NAME(«1:contproc», «2:contname»))",
             state.current_proc.id, state.next_ccid)
end

toc.opschema.GE = schema ""
function toc.opcodes.GE(emit, state, op)
   emit:stmt("return pdcrt_op_ge(marco, PDCRT_CONT_NAME(«1:contproc», «2:contname»))",
             state.current_proc.id, state.next_ccid)
end

toc.opschema.LE = schema ""
function toc.opcodes.LE(emit, state, op)
   emit:stmt("return pdcrt_op_le(marco, PDCRT_CONT_NAME(«1:contproc», «2:contname»))",
             state.current_proc.id, state.next_ccid)
end

toc.opschema.OPEQ = schema ""
function toc.opcodes.OPEQ(emit, state, op)
   emit:stmt("return pdcrt_op_opeq(marco, PDCRT_CONT_NAME(«1:contproc», «2:contname»))",
             state.current_proc.id, state.next_ccid)
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

toc.opschema.ENEW = schema "Ex, Ui"
function toc.opcodes.ENEW(emit, state, op)
   emit:stmt("pdcrt_op_einit(marco, PDCRT_GET_LVAR(«1:localid»), «2:int», pdcrt_objeto_nulo())", op.Ex, op.Ui)
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

toc.opschema.MKARR = schema "Ua"
function toc.opcodes.MKARR(emit, state, op)
   emit:stmt("pdcrt_op_mkarr(marco, «1:int»)", op.Ua)
end

toc.opschema.DYNCALL = schema "Ux, Uy"
function toc.opcodes.DYNCALL(emit, state, op)
   emit:stmt("return pdcrt_op_dyncall(marco, PDCRT_CONT_NAME(«1:contproc», «2:contname»), «3:int», «4:int»)",
             state.current_proc.id, state.next_ccid,
             op.Ux, op.Uy)
end

toc.opschema.CHOOSE = schema "Tx, Ty"
function toc.opcodes.CHOOSE(emit, state, op)
   local cont_consq = state.labels_to_ccid[op.Tx]
   local cont_alt = state.labels_to_ccid[op.Ty]
   emit:toplevelstmt("if(pdcrt_op_choose(marco)) {")
   emit:stmt("PDCRT_CONTINUE(«1:contproc», «2:contname»)", state.current_proc.id, cont_consq)
   emit:toplevelstmt("} else {")
   emit:stmt("PDCRT_CONTINUE(«1:contproc», «2:contname»)", state.current_proc.id, cont_alt)
   emit:toplevelstmt("}")
end

toc.opschema.NAME = schema "Tx"
function toc.opcodes.NAME(emit, state, op)
   -- Nada que hacer.
end

toc.opschema.JMP = schema "Tx"
function toc.opcodes.JMP(emit, state, op)
   emit:stmt("PDCRT_CONTINUE(«1:contproc», «2:contname»)",
             state.current_proc.id, state.labels_to_ccid[op.Tx])
end

toc.opschema.ROT = schema "Ia"
function toc.opcodes.ROT(emit, state, op)
   emit:stmt("pdcrt_op_rot(marco, «1:int»)", op.Ia)
end

toc.opschema.RETN = schema "Ua"
function toc.opcodes.RETN(emit, state, op)
   emit:stmt("PDCRT_RETURN(«1:int»)", op.Ua)
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
   emit:stmt("return pdcrt_op_cmp(marco, PDCRT_CMP_EQ, PDCRT_CONT_NAME(«1:contproc», «2:contname»))",
             state.current_proc.id, state.next_ccid)
end

toc.opschema.CMPNEQ = schema ""
function toc.opcodes.CMPNEQ(emit, state, op)
   emit:stmt("return pdcrt_op_cmp(marco, PDCRT_CMP_NEQ, PDCRT_CONT_NAME(«1:contproc», «2:contname»))",
             state.current_proc.id, state.next_ccid)
end

toc.opschema.CMPREFEQ = schema ""
function toc.opcodes.CMPREFEQ(emit, state, op)
   emit:stmt("return pdcrt_op_cmp(marco, PDCRT_CMP_REFEQ, PDCRT_CONT_NAME(«1:contproc», «2:contname»))",
             state.current_proc.id, state.next_ccid)
end

toc.opschema.MSG = schema "Cx, Ua, Ub"
function toc.opcodes.MSG(emit, state, op)
   emit:stmt("return pdcrt_op_msg(marco, PDCRT_CONT_NAME(«1:contproc», «2:contname»), «3:int», «4:int», «5:int»)",
             state.current_proc.id, state.next_ccid,
             op.Cx, op.Ua, op.Ub)
end

toc.opschema.CLZ2OBJ = schema ""
function toc.opcodes.CLZ2OBJ(emit, state, op)
   emit:stmt("pdcrt_op_clztoobj(marco)")
end

toc.opschema.OBJ2CLZ = schema ""
function toc.opcodes.OBJ2CLZ(emit, state, op)
   emit:stmt("pdcrt_op_objtoclz(marco)")
end

toc.opschema.TMSG = schema "Cx, Ua, Ub"
function toc.opcodes.TMSG(emit, state, op)
   emit:stmt("return pdcrt_op_tail_msg(marco, «1:int», «2:int», «3:int»)", op.Cx, op.Ua, op.Ub)
end

toc.opschema.SPUSH = schema "Ea, Eb"
function toc.opcodes.SPUSH(emit, state, op)
   assert(op.Ea == EACT)
   assert(op.Eb == ESUP)
   emit:stmt("pdcrt_op_spush(marco, PDCRT_ID_EACT, PDCRT_ID_ESUP)")
end

toc.opschema.SPOP = schema "Ea, Eb"
function toc.opcodes.SPOP(emit, state, op)
   assert(op.Ea == EACT)
   assert(op.Eb == ESUP)
   emit:stmt("pdcrt_op_spop(marco, PDCRT_ID_EACT, PDCRT_ID_ESUP)")
end

toc.opschema.OPNEXP = schema "Ua"
function toc.opcodes.OPNEXP(emit, state, op)
   emit:stmt("pdcrt_op_opnexp(marco, «1:int»)", op.Ua)
end

toc.opschema.CLSEXP = schema ""
function toc.opcodes.CLSEXP(emit, state, op)
   emit:stmt("pdcrt_op_clsexp(marco)")
end

toc.opschema.EXP = schema "Cx, La, Bx"
function toc.opcodes.EXP(emit, state, op)
   emit:stmt("pdcrt_op_exp(marco, «1:int», «2:int», «3:bool»)", op.Cx, op.La, op.Bx)
end

toc.opschema.IMPORT = schema "Cx"
function toc.opcodes.IMPORT(emit, state, op)
   local c = state.constants[op.Cx]
   assert(c.type == "string", "constant passed to IMPORT opcode must be a string.")
   emit:comment(("IMPORT %d: %s"):format(op.Cx, c.value))
   emit:stmt("return pdcrt_op_import(marco, «1:int», PDCRT_CONT_NAME(«2:contproc», «3:contname»))",
             op.Cx, state.current_proc.id, state.next_ccid)
end

toc.opschema.SAVEIMPORT = schema "Cx"
function toc.opcodes.SAVEIMPORT(emit, state, op)
   local c = state.constants[op.Cx]
   assert(c.type == "string", "constant passed to SAVEIMPORT opcode must be a string.")
   emit:comment(("SAVEIMPORT %d: %s"):format(op.Cx, c.value))
   emit:stmt("pdcrt_op_saveimport(marco, «1:int»)", op.Cx)
end

toc.opschema.MODULE = schema "Cx"
function toc.opcodes.MODULE(emit, state, op)
   local c = state.constants[op.Cx]
   assert(c.type == "string", "constant passed to MODULE opcode must be a string.")
   emit:comment(("MODULO %d: %s"):format(op.Cx, c.value))
end

-- Fin de los opcodes.

function toc.opcode(emit, state, op, srcloc)
   local errm = "opcode ".. op[1] .. " not implemented"
   emit:comment(("-- %d:%d @ %d --"):format(srcloc.lineno, srcloc.colno, srcloc.byteno))
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

local function gen_module_table(state)
   local module_table = {}
   for id, proc in pairs(state.procedures) do
      for i = 1, #proc.parts do
         local part = proc.parts[i]
         for j = 1, #part do
            local opcode = part[j]
            if opcode[1] == "MODULE" then
               local Cx = opcode[2]
               local c = state.constants[Cx]
               assert(c.type == "string")
               assert(
                  not module_table[Cx],
                  ("redeclared module %s at proc %s (previous was %s)"):format(Cx, id, module_table[Cx])
               )
               module_table[Cx] = id
            end
         end
      end
   end
   return module_table
end

function toc.compmoduletbl(emit, state)
   local module_table = gen_module_table(state)
   if not next(module_table) then
      warnabout("no_modules", "No se declaró ningún módulo")
      emit:stmt("pdcrt_inic_registro_de_modulos(&ctx->registro)")
   else
      local modules = {}
      for Cx, procid in pairs(module_table) do
         modules[#modules + 1] = {
            nombre = autoescape_raw(emit:expr("marco->contexto->constantes.textos[«1:int»]", Cx)),
            cuerpo = autoescape_raw(emit:expr("«1:procname»", procid)),
            valor = autoescape_raw(emit:expr("pdcrt_objeto_nulo()")),
         }
      end
      emit:stmt("pdcrt_modulo modulos[«1:int»] = «2:*structlit»", #modules, modules)
      emit:stmt("pdcrt_inic_registro_de_modulos_con_arreglo(modulos, «1:int», &ctx->registro)", #modules)
   end
end

function toc.comppart(emit, state, part, next_ccid)
   local srclocs = srcposes_to_srclocs(state.source, part.srcposes)
   local substate = attach_extra(state, { next_ccid = next_ccid, srclocs = srclocs })
   for i = 1, #part do
      toc.opcode(emit, substate, part[i], srclocs[i])
   end
   if part.kreq then
      toc.opcode(emit, substate, part.kreq, srclocs[#srclocs])
   end
end

function toc.compparts(emit, state, proc)
   -- ccid = Continuation Id
   local labels_to_ccid = {}
   for i = 1, #proc.parts - 1 do
      local part = proc.parts[i]
      if part.kreq[1] == "NAME" then
         labels_to_ccid[part.kreq[2]] = i + 1
         assert((i + 1) <= #proc.parts, "continuation must follow NAME opcode")
      end
   end

   local substate = attach_extra(state, { labels_to_ccid = labels_to_ccid })

   log.dbg("emitting main proc for %s", proc.id)
   emit:opentoplevel("PDCRT_PROC(«1:localname») {", proc.id)
   local nyo
   if proc.method then
      nyo = 1
   else
      nyo = 0
   end
   emit:stmt("PDCRT_PROC_PRELUDE(«1:localname», «2:int», «3:int», «4:bool»)", proc.id, #proc.params, #proc.params + #proc.locals + nyo, proc.variadic ~= nil)
   if proc.method then
      emit:stmt("PDCRT_PROC_METHOD()")
   end
   for i = 1, #proc.locals do
      local p = proc.locals[i]
      emit:stmt("PDCRT_LOCAL(«1:localid», «1:localname»)", p[2])
   end
   if proc.variadic then
      emit:stmt("PDCRT_PROC_VARIADIC(«1:localname», «2:int», «3:localid»)", proc.id, #proc.params, proc.variadic)
   end
   for i = #proc.params, 1, -1 do
      local p = proc.params[i]
      emit:stmt("PDCRT_PARAM(«1:localid», «1:localname»)", p[2])
   end
   if #proc.parts > 0 then
      log.dbg("emitting main part for procedure %s", proc.id)
      toc.comppart(emit, substate, proc.parts[1], 2)
      log.dbg("emitted")
   else
      warnabout("empty_function", "procedure %s is empty", proc.id)
   end
   if #proc.parts > 1 and contains(FALLS_THROUGHT, proc.parts[1].kreq[1]) then
      emit:stmt("PDCRT_CONTINUE(«1:contproc», «2:contname»)", proc.id, 2)
   else
      emit:stmt("PDCRT_RETURN(0)")
   end
   emit:closetoplevel("}")

   if #proc.parts >= 2 then
      log.dbg("procedure %s has #%d continuations", proc.id, #proc.parts - 1)
   end

   for i = 2, #proc.parts do
      local part = proc.parts[i]
      log.dbg("compiling continuation #%d (%d)", i - 1, i)
      emit:opentoplevel("PDCRT_CONT(«1:contproc», «2:contname») {", proc.id, i)
      emit:stmt("PDCRT_CONT_PRELUDE(«1:contproc», «2:contname»)", proc.id, i)
      toc.comppart(emit, substate, part, i + 1)
      if not part.kreq or contains(FALLS_THROUGHT, part.kreq[1]) then
         if (i + 1) <= #proc.parts then
            emit:stmt("PDCRT_CONTINUE(«1:contproc», «2:contname»)", proc.id, i + 1)
         else
            emit:stmt("PDCRT_RETURN(0)")
         end
      end
      emit:closetoplevel("}")
      log.dbg("finished compilation of continuation")
   end
end

-- Los IDs de procedimientos por encima de BASE_RESERVED_PROC_IDS están reservados.
local BASE_RESERVED_PROC_IDS = 4294967296
-- El ID de procedimiento que se usará para el procedimiento principal.
local MAIN_PROC_ID = BASE_RESERVED_PROC_IDS

function toc.compcode(emit, state)
   local proc = {
      params = {},
      locals = state.code.locals,
      parts = state.code.parts,
      id = MAIN_PROC_ID,
   }
   local extra = {
      in_procedure = false,
      current_proc = proc,
   }

   emit:toplevelstmt("PDCRT_MAIN_CONT_DECLR()")
   emit:opentoplevel("PDCRT_MAIN() {")
   emit:stmt("PDCRT_MAIN_PRELUDE(«1:int»)", 0)
   toc.compconsts(emit, state)
   toc.compmoduletbl(emit, state)
   emit:stmt("PDCRT_RUN(«1:procname»)", MAIN_PROC_ID)
   emit:closetoplevel("}")
   emit:toplevelstmt("PDCRT_MAIN_CONT()")

   toc.compparts(emit, attach_extra(state, extra), proc)
end

function toc.compprocs(emit, state)
   for id, proc in pairs(state.procedures) do
      local extra = {
         in_procedure = true,
         current_proc = proc,
      }
      toc.compparts(emit, attach_extra(state, extra), proc)
   end
end

function toc.compprocdeclrs(emit, state)
   emit:toplevelstmt("PDCRT_DECLARE_PROC(«1:localname»)", MAIN_PROC_ID)
   for kid, part in pairs(state.code.parts) do
      if kid > 1 then
         emit:toplevelstmt("PDCRT_DECLARE_CONT(«1:localname», «2:contname»)", MAIN_PROC_ID, kid)
      end
   end
   for id, proc in pairs(state.procedures) do
      emit:toplevelstmt("PDCRT_DECLARE_PROC(«1:localname»)", id)
      for kid, part in pairs(proc.parts) do
         if kid > 1 then
            emit:toplevelstmt("PDCRT_DECLARE_CONT(«1:localname», «2:contname»)", id, kid)
         end
      end
   end
end


local function checklocals(tp, body)
   local base = 0
   if tp == "proc" then
      for i = 1, #body.params do
         if type(body.params[i][2]) ~= "table" then
            if body.params[i][2] ~= base then
               warnabout("increasing_locals", "non-increasing parameter ID: %s", body.params[i][2])
            end
            base = base + 1
         end
      end
   end
   for i = 1, #body.locals do
      if body.locals[i][2] ~= base then
         warnabout("increasing_locals", "non-increasing local ID: %s", body.locals[i][2])
      end
      base = base + 1
   end
end

local function main(input, config)
   local r = re.compile(grammar)
   log.info("compiling the file")
   local secs = sectionstotable(assert(re.match(input, r), "could not parse bytecode"))
   log.info("done compiling")
   assert(tonumber(secs.version[1]) == VER[1],
          "major version must be " .. tostring(VER[1]))
   assert(tonumber(secs.version[2]) <= VER[2],
          "minor version must be " .. tostring(VER[2]) .. " or less")
   log.info("validated the version")
   if secs.procedures_section then
      local P = {}
      for i = 1, #secs.procedures_section do
         local r = splitcode(prepproc(secs.procedures_section[i]))
         if P[r.id] ~= nil then
            warnabout("redefined_procedure", "duplicate procedure with id #%d", r.id)
         end
         if r.id == MAIN_PROC_ID then
            log.error("Cannot define procedure with MAIN_PROC_ID (%d)", MAIN_PROC_ID)
            os.exit(3)
         end
         P[r.id] = r
      end
      secs.procedures_section = P
   else
      warnabout("no_procedure_section", "no procedure section")
   end
   log.info("extracted procedures")
   local code = splitcode(processcode(codetotable(assert(secs.code_section, "code section not provided"))))
   log.info("processed code")
   if secs.constant_pool_section then
      secs.constant_pool_section = processconstpool(secs.constant_pool_section)
   else
      warnabout("no_constant_pool", "no constant pool")
   end
   log.info("processed constant pool")
   local state = {
      version = secs.version,
      procedures = secs.procedures_section,
      constants = secs.constant_pool_section,
      code = code,
      source = input,
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

MODULE "inicio"

SECTION "code"
  LOCAL 0
  LOCAL 1
  LOCAL 2
  ICONST 0
  LSET 0
  ICONST 1
  LSET 1
  ICONST 2
  LSET 2
  OPNEXP 3
  EXP 0, 0
  EXP 1, 1
  EXP 2, 2
  CLSEXP
ENDSECTION

SECTION "procedures"
ENDSECTION

SECTION "constant pool"
  #0 STRING "Hola"
  #1 STRING "Mundo"
  #2 STRING "QueTal"
ENDSECTION

]=]

-- Fin del ejemplo.

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
   {"l", "link", 0, "Enlaza el archivo principal con todos los demás."},
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
