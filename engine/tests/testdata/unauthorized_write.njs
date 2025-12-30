// njs module that attempts to write to an unauthorized key
exports.meta = {
  name: "unauthorized_write",
  version: "1.0.0",
  reads: [3001],  // score.base
  writes: [3002], // Only score.ml is allowed
  budget: {
    max_write_bytes: 1048576,
    max_write_cells: 100000
  }
};

exports.runBatch = function(objs, ctx, params) {
  // This should FAIL - trying to write to key 3003 which is NOT in meta.writes
  var scores = ctx.batch.writeF32(3003);
  return undefined;
};
