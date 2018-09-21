
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>


extern "C" {


// This macro allows us to prefix strings so that they are less likely to
// conflict with existing symbol names in the examined programs.
// e.g. CGPROF(entry) yields CaLlPrOfIlEr_entry
#define CGPROF(X) CaLlPrOfIlEr_ ## X
#define OUTPUT_FILENAME "profile-results.csv"


	extern uint64_t CGPROF(fnmap_size);
	extern uint64_t CGPROF(csmap_size);

	/* call list */
	extern struct {

		char* srcfile;
		uint64_t line;
		char* caller;
		uint64_t *calls; //array with # of calls to each function

	} CGPROF(csmap)[];

	/* function map */
	extern struct {
		char* fn_name;
		uint64_t fn_ptr;
	} CGPROF(fnmap)[];

	void
	CGPROF(map) (uint64_t fn_ptr, uint64_t fn_id) {
		CGPROF(fnmap)[fn_id].fn_ptr = fn_ptr;
	}

	void
	CGPROF(record) (char *srcfile, uint64_t line, char* caller, uint64_t fnptr) {
		/* index the function map to
			determine which fn_id relates to fn_ptr */

		uint64_t fn_id = 0;
		while (fn_id < CGPROF(fnmap_size)) {
			if (CGPROF(fnmap)[fn_id].fn_ptr == fnptr) {
				break;
				// POST: fn_id matches the fnptr
			}
			fn_id++;
		}

		/* index the call list */
		uint64_t cs_i = 0;
		while (cs_i < CGPROF(csmap_size)) {
			/* comparing the line number is faster */
			if (CGPROF(csmap)[cs_i].line == line &&
				!strcmp(srcfile, CGPROF(csmap)[cs_i].srcfile)) {
				break;
			}
			cs_i++;
		}

		/* increment the array */

		CGPROF(csmap)[cs_i].calls[fn_id]++;
	}

	void
	CGPROF(print) (void) {
		FILE *f = fopen(OUTPUT_FILENAME, "w");
		for (size_t cs_id = 0; cs_id < CGPROF(csmap_size); cs_id++) {
			for (size_t fn_id = 0; fn_id < CGPROF(fnmap_size); fn_id++) {
				auto& info = CGPROF(csmap)[cs_id];
				if (info.calls[fn_id]) {
					fprintf(f, "%s, %s, %lu, %s, %lu\n",
						info.caller, info.srcfile,
						info.line,
						CGPROF(fnmap)[fn_id].fn_name,
						info.calls[fn_id]);
				}
			}
		}
		fclose(f);
	}

}
