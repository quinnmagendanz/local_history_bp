#include <iostream>
#include <stdio.h>
#include <assert.h>
#include "pin.H"
#include <bitset>

static UINT64 takenCorrect = 0;
static UINT64 takenIncorrect = 0;
static UINT64 notTakenCorrect = 0;
static UINT64 notTakenIncorrect = 0;

class BranchPredictor
{	
	public:
        BranchPredictor() { }

        virtual BOOL makePrediction(ADDRINT address) { return FALSE; };

        virtual void makeUpdate(BOOL takenActually, BOOL takenPredicted, ADDRINT address) {};

};


// PASS       | TAKE
// 0b11, 0b10 | 0b01, 0b00
// Used if strict enforcement of space utilization required
///////////////////////////////////////////////
// https://stackoverflow.com/questions/19492682/create-an-array-with-just-2-bit-for-each-cell-in-c
template <size_t N> 
class twoBit
{
		typedef typename std::bitset<2*N>::reference bitRef;
		bitRef a, b;

	public:
		twoBit(bitRef a1, bitRef b1): a(a1), b(b1) {};
		const twoBit &operator=(int i) { a = i%2; b = i/2; return *this; };
		operator int() { return 2*b + a; };
};

template <size_t N> 
class twoBitSet : private std::bitset<2*N>
{
		typedef typename std::bitset<2*N>::reference bitRef;
	public:
	    twoBit<N> operator[](int index)
		{
		    bitRef b1 = std::bitset<2*N>::operator[](2*index);
		    bitRef b2 = std::bitset<2*N>::operator[](2*index + 1);
		    return twoBit<N>(b1, b2);
		};
};

template <size_t N>
class Twobit_Table{
	private:
		twoBitSet<N> table;

		int row(int index) {
			return index % N;
		}

	public:
		bool rd(int index) 
		{
			return !(table[row(index)] & 0b10);
		}

		bool update(int index, bool miss)
		{
			switch (table[row(index)]) {
				case 0 : table[row(index)] = table[row(index)] + miss;
						 break;
				case 1 :
				case 2 : table[row(index)] = table[row(index)] + miss;
						 table[row(index)] = table[row(index)] - !miss;
						 break;
				case 3 : table[row(index)] = table[row(index)] - !miss;
			}

		}
};

template <size_t BIMODAL_SIZE>
class bimodalPredictor: public BranchPredictor
{
	private:
		Twobit_Table<BIMODAL_SIZE> table;

	public:
        BOOL makePrediction(ADDRINT address)
		{
			//return !(TB_READ(index_hash(address) % TB_TABLE_SIZE) & 0b10);
			return table.rd(address);
        }

        void makeUpdate(BOOL takenActually, BOOL takenPredicted, ADDRINT address)
		{
			//if (takenActually) TB_TAKEN(index_hash(address) % TB_TABLE_SIZE);
			//else TB_TAKEN(index_hash(address) % TB_TABLE_SIZE);
			table.update(address, !takenActually);
		}

};
////////////////////////////////////////////

typedef uint64_t HTYPE;

// Hash functions used to index into a table.
inline uint64_t addr_i(uint64_t addr, uint64_t hist) {return addr;}
inline uint64_t xor_i(uint64_t addr, uint64_t hist) {return (addr) ^ hist;}

// W wide bit counter used to make 1-0 prediction.
template <size_t W>
class BitPredictorElement {
		// W <= 64
		uint64_t element;
		
	public:
		bool read() {return element >> (W-1);}
		void inc() {if (element < ((1<<W)-1)) element++;}
		void dec() {if (element != 0) element--;}
};

// Table of L bit predictors. 
template <size_t L, size_t W=2, uint64_t (*hash)(uint64_t addr, uint64_t hist)=(*addr_i)>
class BitPredictor {
		BitPredictorElement<W> table[L];
	
	public:
		// State 0 -> take branch
		// State 1 -> do not take branch
		BOOL get(ADDRINT addr, HTYPE hist=0) {return !table[hash(addr, hist) % L].read();}
		void update(BOOL takenActually, ADDRINT addr, HTYPE hist=0)
		{
			if (takenActually) table[hash(addr, hist) % L].dec();
		    else table[hash(addr, hist) % L].inc();
		}	
};

// Table of L W wide history registers.
template <size_t L, size_t W=10, uint64_t (*hash)(uint64_t addr, uint64_t hist)=(*addr_i)>
class HistoryTable {
	private:
		HTYPE table[L];

	public: 
		HTYPE get(ADDRINT addr, HTYPE hist=0) {
			return table[hash(addr, hist) % L];
		}

		void update(bool takenActually, ADDRINT addr, HTYPE hist=0) {
			table[hash(addr, hist) % L] = ((table[hash(addr, hist) % L] << 1) + !takenActually) & ((1<<W)-1);
		}
};

class BasicPredictor : public BranchPredictor
{
	private:
		BitPredictor<1024, 2> t_table;

	public:
		BOOL makePrediction(ADDRINT address)
		{
			return t_table.get(address);
        }

        void makeUpdate(BOOL takenActually, BOOL takenPredicted, ADDRINT address)
		{	
			t_table.update(takenActually, address);
		}
};

template <size_t GPRED_L, size_t GHIST_SIZE=10, uint64_t (*hash)(uint64_t addr, uint64_t hist)=(*xor_i)>
class GlobalPredictor: public BranchPredictor
{
	private:
		HistoryTable<1, GHIST_SIZE> h_table;
		BitPredictor<GPRED_L, 2, (*hash)> t_table;

	public:
		BOOL makePrediction(ADDRINT address)
		{
			HTYPE glob_hist = h_table.get(0);
			return t_table.get(address, glob_hist);
        }

        void makeUpdate(BOOL takenActually, BOOL takenPredicted, ADDRINT address)
		{	
			HTYPE glob_hist = h_table.get(0);
			t_table.update(takenActually, address, glob_hist);
			h_table.update(takenActually, 0);
		}

		HTYPE getHistory() 
		{
			return h_table.get(0);
		}
};

template <size_t PTABLE_L, size_t LTABLE_L, size_t LHIST_SIZE=10>
class LocalHistoryPredictor: public BranchPredictor
{
	private:
		HistoryTable<LTABLE_L, LHIST_SIZE> h_table;
		BitPredictor<PTABLE_L> t_table;

	public:
		BOOL makePrediction(ADDRINT address)
		{
			HTYPE addr_hist = h_table.get(address);
			return t_table.get(addr_hist);
        }

        void makeUpdate(BOOL takenActually, BOOL takenPredicted, ADDRINT address)
		{	
			HTYPE addr_hist = h_table.get(address);
			t_table.update(takenActually, addr_hist);
			h_table.update(takenActually, address);
		}
};

template <size_t CHOICE_L, size_t LPRED_L, size_t LTABLE_L, size_t GPRED_L, size_t LHIST_SIZE=10, size_t GHIST_SIZE=12>
class TourneyPredictor: public BranchPredictor
{
	private:
		LocalHistoryPredictor<LPRED_L, LTABLE_L, LHIST_SIZE> lhistBP;
		GlobalPredictor<GPRED_L, GHIST_SIZE> globalBP;
		// 1 = localHistory, 0 = gshare
		BitPredictor<CHOICE_L> choice;
		BOOL gPred;
		BOOL lPred;

	public:
		BOOL makePrediction(ADDRINT address)
		{
			gPred = globalBP.makePrediction(address);
			lPred = lhistBP.makePrediction(address);
			HTYPE g_hist = globalBP.getHistory();
			return  choice.get(g_hist) ? lPred : gPred;
        }

        void makeUpdate(BOOL takenActually, BOOL takenPredicted, ADDRINT address)
		{
			HTYPE g_hist = globalBP.getHistory();
			choice.update(g_hist, !(gPred == takenActually));
			choice.update(g_hist, lPred == takenActually);
			globalBP.makeUpdate(takenActually, gPred, address);
			lhistBP.makeUpdate(takenActually, lPred, address);
		}
};

class myBranchPredictor: public BranchPredictor
{
	private:
		//BasicPredictor basicBP;
		//GlobalPredictor<8192, 16> globalBP;
		LocalHistoryPredictor<4096, 2048, 12> lhistBP;
		// 95% 4096, 2048, 12
		//TourneyPredictor<1024, 3000, 2048, 600, 12, 16> tourneyBP;
		// 94.9% 1024, 3000, 2048, 600, 12, 16
		// 94.4% 4096, 1024, 1024, 4096, 10, 12
		// 94.6% 2048, 1700, 2048, 2048, 12, 12

	public:
		BOOL makePrediction(ADDRINT address)
		{
			//return basicBP.makePrediction(address);
			//return globalBP.makePrediction(address);
			return lhistBP.makePrediction(address);
			//return tourneyBP.makePrediction(address);
        }

        void makeUpdate(BOOL takenActually, BOOL takenPredicted, ADDRINT address)
		{
			//basicBP.makeUpdate(takenActually, takenPredicted, address);
			//globalBP.makeUpdate(takenActually, takenPredicted, address);
			lhistBP.makeUpdate(takenActually, takenPredicted, address);
			//tourneyBP.makeUpdate(takenActually, takenPredicted, address);
		}
};


BranchPredictor* BP;


// This knob sets the output file name
KNOB<string> KnobOutputFile(KNOB_MODE_WRITEONCE, "pintool", "o", "result.out", "specify the output file name");


// In examining handle branch, refer to quesiton 1 on the homework
void handleBranch(ADDRINT ip, BOOL direction)
{
    BOOL prediction = BP->makePrediction(ip);
    BP->makeUpdate(direction, prediction, ip);
	takenCorrect += prediction & direction;
	takenIncorrect += prediction & !direction;
	notTakenIncorrect += !prediction & direction;
	notTakenCorrect += !prediction & !direction;
	/*
    if(prediction)
    {
        if(direction)
        {
            takenCorrect++;
        }
        else
        {
            takenIncorrect++;
        }
    }
    else
    {
        if(direction)
        {
            notTakenIncorrect++;
        }
        else
        {
            notTakenCorrect++;
        }
    }
	*/
}


void instrumentBranch(INS ins, void * v)
{
    if(INS_IsBranch(ins) && INS_HasFallThrough(ins))
    {
        INS_InsertCall(
                ins, IPOINT_TAKEN_BRANCH, (AFUNPTR)handleBranch,
                IARG_INST_PTR,
                IARG_BOOL,
                TRUE,
                IARG_END);

        INS_InsertCall(
                ins, IPOINT_AFTER, (AFUNPTR)handleBranch,
                IARG_INST_PTR,
                IARG_BOOL,
                FALSE,
                IARG_END);
    }
}


/* ===================================================================== */
VOID Fini(int, VOID * v)
{
    FILE* outfile;
    assert(outfile = fopen(KnobOutputFile.Value().c_str(),"w"));
    fprintf(outfile, "takenCorrect %lu takenIncorrect %lu notTakenCorrect %lu notTakenIncorrect %lu\n", takenCorrect, takenIncorrect, notTakenCorrect, notTakenIncorrect);
	// TODO(magendanz) remove before submitting.
	fprintf(outfile, "Correctness: %lu%%\n", (100lu*(takenCorrect + notTakenCorrect))/(takenCorrect + takenIncorrect + notTakenCorrect + notTakenIncorrect));
}


// argc, argv are the entire command line, including pin -t <toolname> -- ...
int main(int argc, char * argv[])
{
    // Make a new branch predictor
    BP = new myBranchPredictor();

    // Initialize pin
    PIN_Init(argc, argv);

    // Register Instruction to be called to instrument instructions
    INS_AddInstrumentFunction(instrumentBranch, 0);

    // Register Fini to be called when the application exits
    PIN_AddFiniFunction(Fini, 0);

    // Start the program, never returns
    PIN_StartProgram();

    return 0;
}

