'''Pretty-printers para los distíntos tipos del runtime de PseudoD.

Al activar esta extensión (llamando a la función `register_pretty_printers()`)
podrás usar el comando `print` para ver valores del runtime.

Esta extensión de GDB requiere Python 3.
'''
import re
import gdb


LOCALES_ESP = ['ESUP', 'EACT']
PDCRT_NUM_LOCALES_ESP = 2


class PDCRT_Texto:
    def __init__(self, val):
        self.val = val

    def to_string(self):
        return self.val['contenido'].string(
            encoding='utf8',
            errors='strict',
            length=self.val['longitud']
        )

    def display_hint(self):
        return 'string'


class PDCRT_Objeto:
    TAG_INT_TYPE = gdb.lookup_type('int')

    MAPPINGS = {
        0: ('i', False),
        1: ('f', False),
        2: (None, False),
        3: ('c', False),
        4: ('t', True),
        5: ('o', False),
        6: ('b', False),
        7: (None, False),
    }

    def __init__(self, val):
        self.val = val

    def children(self):
        tag = self.val['tag']
        yield 'tag', tag
        tag_int = int(tag.cast(self.TAG_INT_TYPE))
        value_field, deref = self.MAPPINGS.get(tag_int, (None, False))
        if value_field is not None:
            real_val = self.val['value'][value_field]
            if deref:
                real_val = real_val.dereference()
            yield 'value.{}'.format(value_field), real_val
        yield 'recv', self.val['recv']

    def display_hint(self):
        return None

    def to_string(self):
        return str(self.val.type)


class PDCRT_Continuacion:
    TAG_INT_TYPE = gdb.lookup_type("int")

    MAPPINGS = {
        0: 'iniciar',
        1: 'continuar',
        2: None,
        3: 'enviar_mensaje',
        4: 'tail_iniciar',
        5: 'tail_enviar_mensaje',
    }

    def __init__(self, val):
        self.val = val

    def display_hint(self):
        return None

    def to_string(self):
        return str(self.val.type)

    def children(self):
        tag = self.val['tipo']
        yield 'tag', tag
        tag_int = int(tag.cast(self.TAG_INT_TYPE))
        value_field = self.MAPPINGS.get(tag_int, None)
        if value_field is not None:
            real_val = self.val['valor'][value_field]
            yield 'valor.{}'.format(value_field), real_val


class PDCRT_Env:
    def __init__(self, val):
        self.val = val

    def display_hint(self):
        return None

    def _env_size(self):
        return int(self.val['env_size']) - PDCRT_NUM_LOCALES_ESP

    def to_string(self):
        return '{} con {} (+{}) elementos'.format(str(self.val.type), self._env_size(), PDCRT_NUM_LOCALES_ESP)

    def children(self):
        size = self._env_size() + PDCRT_NUM_LOCALES_ESP
        for i in range(size):
            if i < PDCRT_NUM_LOCALES_ESP:
                label = '[{}: env[{}]]'.format(LOCALES_ESP[i], i)
            else:
                label = '[{}: env[{}]]'.format(i - PDCRT_NUM_LOCALES_ESP, i)
            yield label, self.val['env'][i]


class PDCRT_Marco:
    def __init__(self, val):
        self.val = val

    def display_hint(self):
        return None

    def _num_locales(self):
        return int(self.val['num_locales'])

    def to_string(self):
        return '{} con {} (+{}) locales'.format(str(self.val.type), self._num_locales(), PDCRT_NUM_LOCALES_ESP)

    def children(self):
        yield 'contexto', self.val['contexto']
        yield 'marco_anterior', self.val['marco_anterior']
        size = self._num_locales() + PDCRT_NUM_LOCALES_ESP
        for i in range(size):
            if i < PDCRT_NUM_LOCALES_ESP:
                label = '[{}: locales[{}]]'.format(LOCALES_ESP[i], i)
            else:
                label = '[{}: locales[{}]]'.format(i - PDCRT_NUM_LOCALES_ESP, i)
            yield label, self.val['locales'][i]


class PDCRT_Pila:
    def __init__(self, val):
        self.val = val

    def display_hint(self):
        return 'array'

    def to_string(self):
        return '{} con {} (de {}) elementos'.format(str(self.val.type), self.val['num_elementos'], self.val['capacidad'])

    def children(self):
        for i in range(int(self.val['num_elementos'])):
            yield '[{}]'.format(i), self.val['elementos'][i]


def lookup_function(val):
    if val.type.strip_typedefs().tag == 'pdcrt_texto':
        return PDCRT_Texto(val)
    elif val.type.strip_typedefs().tag == 'pdcrt_objeto':
        return PDCRT_Objeto(val)
    elif val.type.strip_typedefs().tag == 'pdcrt_env':
        return PDCRT_Env(val)
    elif val.type.strip_typedefs().tag == 'pdcrt_continuacion':
        return PDCRT_Continuacion(val)
    elif val.type.strip_typedefs().tag == 'pdcrt_marco':
        return PDCRT_Marco(val)
    elif val.type.strip_typedefs().tag == 'pdcrt_pila':
        return PDCRT_Pila(val)
    else:
        return None


def gdb_command(cmd_name, completion):
    def wrapper(f):
        class Command(gdb.Command):
            def __init__(self):
                super().__init__(cmd_name, completion)

            def invoke(self, *args, **kwargs):
                return f(*args, **kwargs)
        Command.__doc__ = f.__doc__
        Command()
        return Command
    return wrapper


def dereference_everything(val):
    while val.type.code == gdb.TYPE_CODE_PTR:
        val = val.dereference()
    return val


@gdb_command('pdcrt-marco', gdb.COMMAND_DATA)
def inspeccionar_marco(arg, from_tty):
    val = gdb.parse_and_eval(arg)
    try:
        frame_index = -1
        while True:
            frame_index = frame_index + 1
            val = dereference_everything(val)
            if val.type.strip_typedefs().tag != 'pdcrt_marco':
                print('El valor no es ni apunta a un pdcrt_marco')
                break
            else:
                print('#{}: contexto = {}, marco_anterior = {}'.format(frame_index, val['contexto'], val['marco_anterior']))
                size = int(val['num_locales'])
                for i in range(size):
                    if i < PDCRT_NUM_LOCALES_ESP:
                        name = LOCALES_ESP[i]
                    else:
                        name = int(i - PDCRT_NUM_LOCALES_ESP)
                    print('[{}: locales[{}]]: {}'.format(name, i, val['locales'][i]))
                print()
                val = val['marco_anterior']
    except gdb.MemoryError:
        pass


def register_pretty_printers(objfile):
    objfile.pretty_printers.append(lookup_function)
