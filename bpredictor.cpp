#include <iostream>
#include <stdio.h>
#include <assert.h>
#include "pin.H"

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
#define TB_TABLE_SIZE 16384
#define TB_INDEX(addr) (addr >> 2) & (TB_TABLE_SIZE - 1)
#define TB_READ(index) tb_table[index >> 2] >> ((index & 0b11) << 1)
#define TB_PASSED(index) tb_table[index >> 2] &= (((TB_READ(index) >> 1) | 0b10) << ((index & 0b11) << 1))
#define TB_TAKEN(index) tb_table[index >> 2] &= (((TB_READ(index) << 1) & 0b11) << ((index & 0b11) << 1))

class Twobit_Table{
	private:
		unsigned char* table;
		size_t length;

		int row(int index) {
			return index % length;
		}

	public:
		Twobit_Table(size_t t_length)
		{
			length = t_length;
			table = new unsigned char[length];
		}

		bool rd(int index) 
		{
			return !(table[row(index)] & 0b10);
		}

		bool update(int index, bool miss)
		{
			switch (table[row(index)]) {
				case 0 : table[row(index)] += miss;
						 break;
				case 1 :
				case 2 : table[row(index)] += miss;
						 table[row(index)] -= !miss;
						 break;
				case 3 : table[row(index)] -= !miss;
			}

		}
};

// 0 = Taken, 1 = Not
typedef unsigned char HTYPE;

class History_Table {
	private:
		HTYPE* table;
		size_t length;

	public: 
		History_Table(size_t t_length) {
			length = t_length;
			table = new HTYPE[length];
		}

		HTYPE get(int index) {
			return table[index % length];
		}

		void update(int index, bool event) {
			table[index % length] = (table[index % length] << 1) + event;
		}
};

class bimodalPredictor: public BranchPredictor
{
	private:
		Twobit_Table* table;

	public:
        bimodalPredictor() 
		{
			table = new Twobit_Table(TB_TABLE_SIZE);
		}

        BOOL makePrediction(ADDRINT address)
		{
			//return !(TB_READ(index_hash(address) % TB_TABLE_SIZE) & 0b10);
			return table->rd(address);
        }

        void makeUpdate(BOOL takenActually, BOOL takenPredicted, ADDRINT address)
		{
			//if (takenActually) TB_TAKEN(index_hash(address) % TB_TABLE_SIZE);
			//else TB_TAKEN(index_hash(address) % TB_TABLE_SIZE);
			table->update(address, !takenActually);
		}

};

#define GTABLE_SIZE 4096

class gsharePredictor: public BranchPredictor
{
	private:
		History_Table* h_table;
		Twobit_Table* t_table;

	public:
		gsharePredictor() 
		{
			t_table = new Twobit_Table(GTABLE_SIZE);
			h_table = new History_Table(1);
		}

		BOOL makePrediction(ADDRINT address)
		{
			HTYPE glob_hist = h_table->get(0);
			return t_table->rd(HTYPE (address) ^ glob_hist);
        }

        void makeUpdate(BOOL takenActually, BOOL takenPredicted, ADDRINT address)
		{	
			HTYPE glob_hist = h_table->get(0);
			t_table->update(HTYPE (address) ^ glob_hist, !takenActually);
			h_table->update(0, !takenActually);
		}
};

#define PTABLE_SIZE 4096
#define LHIST_SIZE 4096

class localHistoryPredictor: public BranchPredictor
{
	private:
		History_Table* h_table;
		Twobit_Table* t_table;

	public:
		localHistoryPredictor() 
		{
			t_table = new Twobit_Table(PTABLE_SIZE);
			h_table = new History_Table(LHIST_SIZE);
		}

		BOOL makePrediction(ADDRINT address)
		{
			HTYPE addr_hist = h_table->get(address);
			return t_table->rd(addr_hist);
        }

        void makeUpdate(BOOL takenActually, BOOL takenPredicted, ADDRINT address)
		{	
			HTYPE addr_hist = h_table->get(address);
			t_table->update(addr_hist, !takenActually);
			h_table->update(address, !takenActually);
		}
};

class myBranchPredictor: public BranchPredictor
{
	private:
		//BranchPredictor* biBP;
		//BranchPredictor* gshareBP;
		BranchPredictor* lhistBP;

	public:
		myBranchPredictor() 
		{
			//biBP = new bimodalPredictor();
			//gshareBP = new gsharePredictor();
			lhistBP = new localHistoryPredictor();
		}

		BOOL makePrediction(ADDRINT address)
		{
			//return biBP->makePrediction(address);
			//return gshareBP->makePrediction(address);
			return lhistBP->makePrediction(address);
        }

        void makeUpdate(BOOL takenActually, BOOL takenPredicted, ADDRINT address)
		{
			//biBP->makeUpdate(takenActually, takenPredicted, address);
			//gshareBP->makeUpdate(takenActually, takenPredicted, address);
			lhistBP->makeUpdate(takenActually, takenPredicted, address);
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

