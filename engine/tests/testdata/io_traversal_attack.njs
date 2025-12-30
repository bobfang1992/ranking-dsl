// Module that tries path traversal attack
exports.meta = {
  name: "io_traversal_test",
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
  // Try to read CSV with path traversal
  var csv = ctx.io.readCsv(params.csv_file);  // Should throw
  return undefined;
};
