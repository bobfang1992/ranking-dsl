// Module that requests IO capability (csv_read)
exports.meta = {
  name: "io_capability_test",
  version: "1.0.0",
  reads: [Keys.SCORE_BASE],
  writes: [Keys.SCORE_ML],
  capabilities: {
    io: {
      csv_read: true
    }
  },
  budget: {
    max_write_bytes: 1048576,
    max_write_cells: 100000,
    max_io_read_bytes: 1048576,
    max_io_read_rows: 1000
  }
};

exports.runBatch = function(objs, ctx, params) {
  // Try to read CSV via ctx.io
  if (!ctx.io) {
    throw new Error("ctx.io not available");
  }

  var csv = ctx.io.readCsv(params.csv_file);

  // Write result count as score to prove it worked
  var n = ctx.batch.rowCount();
  var output = ctx.batch.writeF32(Keys.SCORE_ML);
  for (var i = 0; i < n; i++) {
    output[i] = csv.rowCount;  // Write the row count from CSV
  }

  return undefined;
};
