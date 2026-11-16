#define CHIPS_IMPL
#include "z80.h"
#include "z80ctc.h"


// Event-runtime adapters preserve the pinned CTC's I/O and daisy semantics.
extern "C" uint64_t eii_ctc_io(z80ctc_t* ctc,uint64_t pins) {return _z80ctc_iorq(ctc,pins);}
extern "C" uint64_t eii_ctc_zero(z80ctc_t* ctc,unsigned ch,uint64_t pins) {return _z80ctc_counter_zero(ctc,&ctc->chn[ch],pins,int(ch));}
extern "C" uint64_t eii_ctc_irq(z80ctc_t* ctc,uint64_t pins) {return _z80ctc_int(ctc,pins);}
