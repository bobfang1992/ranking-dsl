// njs module that exceeds the budget
exports.meta = {
  name: "budget_exceeded",
  version: "1.0.0",
  reads: [3001],
  writes: [3002, 3003, 3004], // Multiple write keys allowed
  budget: {
    max_write_bytes: 1000000,  // High bytes limit
    max_write_cells: 10        // Very small - only 10 cells allowed
  }
};

exports.runBatch = function(objs, ctx, params) {
  // Allocating 100 cells should exceed the 10 cell limit
  var scores = ctx.batch.writeF32(3002);
  return undefined;
};
