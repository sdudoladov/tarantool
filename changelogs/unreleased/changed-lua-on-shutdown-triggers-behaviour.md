## feature/core

*  Previously lua on_shutdown triggers were started sequentially,
   now each of triggers starts in a separate fiber and then we waits
   3.0 seconds to their completion.