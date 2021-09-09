local re = require "re"
local v = require "fennelview"

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
oparg <- flt / int / str

code <- {| '' -> 'locals'  (rs local)* |}
        {| '' -> 'opcodes'  (rs opcode)* |}
local <- {| {"LOCAL"} rs id |}

OP <- "LCONST" / "ICONST" / "FCONST" / "CALL" / "SUM" / "SUB"
    / "MUL" / "DIV" / "LSET" / "LGET" / "RETN" / "RET"

procsec <- {| '' -> 'procedures_section'
              "SECTION" ws '"procedures"' (rs proc)* rs "ENDSECTION" |}
proc <- {| '' -> 'proc'
           "PROC" rs id {| '' -> 'params'  (rs param)* |} code rs "ENDPROC" |}
param <- {| {"PARAM"} rs id |}

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


local function processoparg(tbl)
   assert(type(tbl) == "table", "expected table")
   if tbl.int then
      return tonumber(tbl.int)
   elseif tbl.flt then
      return tonumber(tbl.flt)
   elseif tbl.id then
      return tonumber(tbl.id)
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
      assert(pool[id] == nil, "cannot redefine constant " .. tostring(id))
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
      assert(#s == #op - 1)
      for i = 1, #s do
         local c = string.sub(s, i, i)
         local a = op[i + 1]
         if c == "i" then
            assert(math.type(a) == "integer", "expected integer")
         elseif c == "f" then
            assert(math.type(a) == "float", "expected float")
         elseif c == "n" then
            assert(type(a) == "number", "expected number")
         elseif c == "u" then
            assert(type(a) == "number" and a >= 0, "expected non-negative number")
         elseif c == "s" then
            assert(type(a) == "string", "expected string")
         elseif c == "d" then
            assert(math.type(a) == "integer" and a >= 0, "expected id")
         else
            error("unrecognized schema value")
         end
      end
   end
end

function toc.makeemitter()
   local emit = {
      statments = {},
   }

   function emit:_formatsingle(arg, spec)
      if spec == "int" then
         assert(math.type(arg) == "integer", "expected integer")
         -- Emit arg as a C integer. tostring() is not correct, but should do
         -- for now
         return tostring(arg)
      elseif spec == "flt" then
         assert(math.type(arg) == "float", "expected float")
         -- Same caveat
         return tostring(arg)
      elseif spec == "id" then
         assert(math.type(arg) == "integer" and arg >= 0, "expected integer")
         return "name_" .. tostring(arg)
      elseif spec == "strlit" then
         assert(type(arg) == "string", "expected string")
         -- String literal. Again, not perfect.
         return string.format("%q", arg)
      else
         error("unknown specifier " .. spec)
      end
   end

   function emit:_basic(fmt, ...)
      local last = 1
      local res = {}
      for prefixpos, argpos, spec, suffixpos in string.gmatch(fmt, "()«([0-9]+):(%w+)»()") do
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
   emit:stmt("pdcrt_op_iconst(ctx, «1:int»)", op[2])
end

toc.opschema.LCONST = schema "d"
function toc.opcodes.LCONST(emit, state, op)
   local c = state.constants[op[2]]
   local t, v = c.type, c.value
   if t == "string" then
      emit:stmt("pdcrt_op_lconst_string(ctx, «2:strlit»)", op[2], v)
   else
      emit:stmt("pdcrt_op_lconst_int(ctx, «2:int»)", op[2], v)
   end
end

toc.opschema.SUM = schema ""
function toc.opcodes.SUM(emit, state, op)
   emit:stmt("pdcrt_op_sum(ctx)")
end

toc.opschema.MUL = schema ""
function toc.opcodes.MUL(emit, state, op)
   emit:stmt("pdcrt_op_mul(ctx)")
end

toc.opschema.SUB = schema ""
function toc.opcodes.SUB(emit, state, op)
   emit:stmt("pdcrt_op_sub(ctx)")
end

toc.opschema.DIV = schema ""
function toc.opcodes.DIV(emit, state, op)
   emit:stmt("pdcrt_op_div(ctx)")
end

toc.opschema.CALL = schema "duu"
function toc.opcodes.CALL(emit, state, op)
   emit:stmt("pdcrt_op_call(ctx, PDCRT_PROC_NAME(ctx, «1:id»), «2:int», «3:int»)", op[2], op[3], op[4])
end

toc.opschema.LGET = schema "d"
function toc.opcodes.LGET(emit, state, op)
   emit:stmt("pdcrt_op_lget(ctx, PDCRT_GET_LVAR(ctx, «1:id»))", op[2])
end

toc.opschema.LSET = schema "d"
function toc.opcodes.LSET(emit, state, op)
   emit:stmt("PDCRT_SET_LVAR(ctx, «1:id», pdcrt_op_lset(ctx))", op[2])
end

toc.opschema.RETN = schema "u"
function toc.opcodes.RETN(emit, state, op)
   emit:stmt("pdcrt_op_retn(ctx, «1:int»)", op[2])
   assert(state.in_procedure, "RETN can only appear inside a procedure")
   emit:stmt("PDCRT_PROC_POSTLUDE(ctx, «1:id»)", state.current_proc.id)
   emit:stmt("return pdcrt_real_return(ctx)")
end

function toc.opcode(emit, state, op)
   local errm = "opcode ".. op[1] .. " not implemented"
   assert(toc.opschema[op[1]], errm)(op)
   return assert(toc.opcodes[op[1]], errm)(emit, state, op)
end

function toc.compcode(emit, state)
   emit:opentoplevel("PDCRT_MAIN() {")
   emit:stmt("PDCRT_MAIN_PRELUDE()")
   for i = 1, #state.code.locals do
      local p = state.code.locals[i]
      emit:stmt("PDCRT_LOCAL(ctx, «1:int», «2:id»)", i, p[2])
   end
   for i = 1, #state.code.opcodes do
      toc.opcode(emit, state, state.code.opcodes[i])
   end
   emit:stmt("PDCRT_MAIN_POSTLUDE()")
   emit:closetoplevel("}")
end

function toc.opproc(emit, state, proc)
   emit:opentoplevel("PDCRT_PROC(«1:id») {", proc.id)
   emit:stmt("PDCRT_PROC_PRELUDE(ctx, «1:id»)", proc.id)
   emit:stmt("PDCRT_ASSERT_PARAMS(ctx, «1:int»)", #proc.params)
   for i = 1, #proc.params do
      local p = proc.params[i]
      emit:stmt("PDCRT_PARAM(ctx, «1:int», «2:id»)", i, p[2])
   end
   for i = 1, #proc.locals do
      local p = proc.locals[i]
      emit:stmt("PDCRT_LOCAL(ctx, «1:int», «2:id»)", i, p[2])
   end
   for i = 1, #proc.opcodes do
      toc.opcode(emit, state, proc.opcodes[i])
   end
   emit:stmt("PDCRT_PROC_POSTLUDE(ctx, «1:id»)", proc.id)
   emit:stmt("return pdcrt_passthru_return(ctx)")
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
      emit:toplevelstmt("PDCRT_DECLARE_PROC(«1:id»)", id)
   end
end


local sample = [=[

PDVM 1.0
PLATFORM "pdcrt"

SECTION "code"
  ICONST 3
  CALL 5, 1, 1
ENDSECTION

SECTION "constant pool"
ENDSECTION

SECTION "procedures"
  PROC 2
    PARAM 3
    PARAM 4
    LGET 3
    LGET 4
    SUM
    RETN 1
  ENDPROC

  PROC 5
    PARAM 7
    LGET 7
    ICONST 5
    MUL
    ICONST 4
    CALL 6, 1, 1
    SUM
    RETN 1
  ENDPROC

  PROC 6
    PARAM 8
    LGET 8
    ICONST 1
    SUM
    RETN 1
  ENDPROC
ENDSECTION

;; SECTION "exports"
;;   EXPORT PROC 2 NAMED "hello_world" (CCONV "C")
;;   EXPORT CODE NAMED "my_main" (CCONV "C")
;; ENDSECTION

]=]

local r = re.compile(grammar)
local secs = sectionstotable(assert(re.match(sample, r), "could not parse bytecode"))
assert(secs.version[1] == "1", "major version must be 1")
assert(tonumber(secs.version[2]) <= 0, "minor version must be 0 or less")
if secs.procedures_section then
   local P = {}
   for i = 1, #secs.procedures_section do
      local r = prepproc(secs.procedures_section[i])
      P[r.id] = r
   end
   secs.procedures_section = P
end
local code = processcode(codetotable(assert(secs.code_section, "code section not provided")))
local state = {
   version = secs.version,
   procedures = secs.procedures_section,
   constants = processconstpool(secs.constant_pool_section),
   code = code,
}
--pv(secs)
--pv(state)
local emit = toc.makeemitter()
emit:include("pdcrt.h")
toc.compprocdeclrs(emit, state)
toc.compcode(emit, state)
toc.compprocs(emit, state)
print(emit:emittedstmts())
