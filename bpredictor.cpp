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

// 0 = Taken, 1 = Not
typedef unsigned short HTYPE;

template <size_t N>
class History_Table {
	private:
		HTYPE* table;

	public: 
		History_Table() {
			table = new HTYPE[N];
		}

		HTYPE get(int index) {
			return table[index % N];
		}

		void update(int index, bool event) {
			table[index % N] = (table[index % N] << 1) + event;
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

template <size_t GTABLE_SIZE>
class gsharePredictor: public BranchPredictor
{
	private:
		History_Table<1> h_table;
		Twobit_Table<GTABLE_SIZE> t_table;

	public:
		BOOL makePrediction(ADDRINT address)
		{
			HTYPE glob_hist = h_table.get(0);
			return t_table.rd(HTYPE (address) ^ glob_hist);
        }

        void makeUpdate(BOOL takenActually, BOOL takenPredicted, ADDRINT address)
		{	
			HTYPE glob_hist = h_table.get(0);
			t_table.update(HTYPE (address) ^ glob_hist, !takenActually);
			h_table.update(0, !takenActually);
		}

		HTYPE getHistory() 
		{
			return h_table.get(0);
		}
};

template <size_t PTABLE_SIZE, size_t LHIST_SIZE>
class localHistoryPredictor: public BranchPredictor
{
	private:
		History_Table<PTABLE_SIZE> h_table;
		Twobit_Table<LHIST_SIZE> t_table;

	public:
		BOOL makePrediction(ADDRINT address)
		{
			HTYPE addr_hist = h_table.get(address>>2);
			return t_table.rd(addr_hist);
        }

        void makeUpdate(BOOL takenActually, BOOL takenPredicted, ADDRINT address)
		{	
			HTYPE addr_hist = h_table.get(address>>2);
			t_table.update(addr_hist, !takenActually);
			h_table.update(address>>2, !takenActually);
		}
};

template <size_t CHOICE_SIZE>
class tourneyPredictor: public BranchPredictor
{
	private:
		localHistoryPredictor<4096, 4096> lhistBP;
		gsharePredictor<4096> gshareBP;
		// 0 = localHistory, 1 = gshare
		Twobit_Table<CHOICE_SIZE> choice;
		BOOL gPred;
		BOOL lPred;

	public:
		BOOL makePrediction(ADDRINT address)
		{
			gPred = gshareBP.makePrediction(address);
			lPred = lhistBP.makePrediction(address);
			return  choice.rd(address) ? gPred : lPred;
        }

        void makeUpdate(BOOL takenActually, BOOL takenPredicted, ADDRINT address)
		{
			choice.update(address, !(gPred == takenActually));
			choice.update(address, lPred == takenActually);
			gshareBP.makeUpdate(takenActually, gPred, address);
			lhistBP.makeUpdate(takenActually, lPred, address);
		}
};

class myBranchPredictor: public BranchPredictor
{
	private:
		//BranchPredictor* biBP;
		//BranchPredictor* gshareBP;
		localHistoryPredictor<1024, 1024> lhistBP;
		//BranchPredictor* tourneyBP;

	public:
		myBranchPredictor() 
		{
			//biBP = new bimodalPredictor();
			//gshareBP = new gsharePredictor();
			//lhistBP = new localHistoryPredictor();
			//tourneyBP = new tourneyPredictor();
		}

		BOOL makePrediction(ADDRINT address)
		{
			//return biBP->makePrediction(address);
			//return gshareBP->makePrediction(address);
			return lhistBP.makePrediction(address);
			//return tourneyBP->makePrediction(address);
        }

        void makeUpdate(BOOL takenActually, BOOL takenPredicted, ADDRINT address)
		{
			//biBP->makeUpdate(takenActually, takenPredicted, address);
			//gshareBP->makeUpdate(takenActually, takenPredicted, address);
			lhistBP.makeUpdate(takenActually, takenPredicted, address);
			//tourneyBP->makeUpdate(takenActually, takenPredicted, address);
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
	fprintf(outfile, "Correctness: %lu%\n", (100*(takenCorrect + notTakenCorrect))/(takenCorrect + takenIncorrect + notTakenCorrect + notTakenIncorrect));
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

