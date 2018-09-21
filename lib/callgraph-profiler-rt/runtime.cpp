
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

	/* --- data --- */
	extern uint64_t CGPROF(fnmap_size);
	extern uint64_t CGPROF(csmap_size);

	/* call list */
	extern struct {

		char* srcfile;
		uint64_t line;
		char* caller;

	} CGPROF(csmap)[];

	/* call counter */
	extern uint64_t CGPROF(calls)[];

	/* function map */
	extern struct {
		char* fn_name;
		uint64_t fn_ptr;
	} CGPROF(fnmap)[];

	/* --- functions --- */
	void
	CGPROF(map) (uint64_t fn_ptr, uint64_t fn_id) {
		CGPROF(fnmap)[fn_id].fn_ptr = fn_ptr;
	}

	void
	CGPROF(record) (uint64_t cs_id, uint64_t fnptr) {
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

		/* increment the array */

		CGPROF(calls)[cs_id * CGPROF(fnmap_size) + fn_id]++;
	}

	void
	CGPROF(print) (void) {
		FILE *f = fopen(OUTPUT_FILENAME, "w");
		for (size_t cs_id = 0; cs_id < CGPROF(csmap_size) + 1; cs_id++) {
			for (size_t fn_id = 0; fn_id < CGPROF(fnmap_size); fn_id++) {
				uint64_t count = CGPROF(calls)[cs_id * CGPROF(fnmap_size) + fn_id];
				if (count) {
					auto& info = CGPROF(csmap)[cs_id];
					fprintf(f, "%s, %s, %lu, %s, %lu\n",
						info.caller,
						info.srcfile,
						info.line,
						CGPROF(fnmap)[fn_id].fn_name,
						count);
				}
			}
		}
		fclose(f);
	}

}
