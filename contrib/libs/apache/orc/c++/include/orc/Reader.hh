/**
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef ORC_READER_HH
#define ORC_READER_HH

#include "orc/BloomFilter.hh"
#include "orc/Common.hh"
#include "orc/Statistics.hh"
#include "orc/Type.hh"
#include "orc/Vector.hh"
#include "orc/orc-config.hh"
#include "orc/sargs/SearchArgument.hh"

#include <atomic>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace orc {

  // classes that hold data members so we can maintain binary compatibility
  struct ReaderOptionsPrivate;
  struct RowReaderOptionsPrivate;

  struct CacheOptions {
    // The maximum distance in bytes between two consecutive
    // ranges; beyond this value, ranges are not combined
    uint64_t holeSizeLimit = 8192;

    // The maximum size in bytes of a combined range; if
    // combining two consecutive ranges would produce a range of a
    // size greater than this, they are not combined
    uint64_t rangeSizeLimit = 32 * 1024 * 1024;
  };

  /**
   * Expose the reader metrics including the latency and
   * number of calls of the decompression/decoding/IO modules.
   */
  struct ReaderMetrics {
    std::atomic<uint64_t> ReaderCall{0};
    // ReaderInclusiveLatencyUs contains the latency of
    // the decompression/decoding/IO modules.
    std::atomic<uint64_t> ReaderInclusiveLatencyUs{0};
    std::atomic<uint64_t> DecompressionCall{0};
    std::atomic<uint64_t> DecompressionLatencyUs{0};
    std::atomic<uint64_t> DecodingCall{0};
    std::atomic<uint64_t> DecodingLatencyUs{0};
    std::atomic<uint64_t> ByteDecodingCall{0};
    std::atomic<uint64_t> ByteDecodingLatencyUs{0};
    std::atomic<uint64_t> IOCount{0};
    std::atomic<uint64_t> IOBlockingLatencyUs{0};
    std::atomic<uint64_t> SelectedRowGroupCount{0};
    std::atomic<uint64_t> EvaluatedRowGroupCount{0};
    std::atomic<uint64_t> ReadRangeCacheHits{0};
    std::atomic<uint64_t> ReadRangeCacheMisses{0};
  };
  ReaderMetrics* getDefaultReaderMetrics();

  // Row group index of a single column in a stripe.
  struct RowGroupIndex {
    // Positions are represented as a two-dimensional array where the first
    // dimension is row group index and the second dimension is the position
    // list of the row group. The size of the second dimension should be equal
    // among all row groups.
    std::vector<std::vector<uint64_t>> positions;
  };

  /**
   * Options for creating a Reader.
   */
  class ReaderOptions {
   private:
    std::unique_ptr<ReaderOptionsPrivate> privateBits_;

   public:
    ReaderOptions();
    ReaderOptions(const ReaderOptions&);
    ReaderOptions(ReaderOptions&);
    ReaderOptions& operator=(const ReaderOptions&);
    virtual ~ReaderOptions();

    /**
     * Set the stream to use for printing warning or error messages.
     */
    ReaderOptions& setErrorStream(std::ostream& stream);

    /**
     * Set a serialized copy of the file tail to be used when opening the file.
     *
     * When one process opens the file and other processes need to read
     * the rows, we want to enable clients to just read the tail once.
     * By passing the string returned by Reader.getSerializedFileTail(), to
     * this function, the second reader will not need to read the file tail
     * from disk.
     *
     * @param serialization the bytes of the serialized tail to use
     */
    ReaderOptions& setSerializedFileTail(const std::string& serialization);

    /**
     * Set the memory allocator.
     */
    ReaderOptions& setMemoryPool(MemoryPool& pool);

    /**
     * Set the reader metrics.
     *
     * Defaults to nullptr.
     * When set to nullptr, the reader metrics will be disabled.
     */
    ReaderOptions& setReaderMetrics(ReaderMetrics* metrics);

    /**
     * Set the cache options.
     */
    ReaderOptions& setCacheOptions(const CacheOptions& cacheOptions);

    /**
     * Set the location of the tail as defined by the logical length of the
     * file.
     */
    ReaderOptions& setTailLocation(uint64_t offset);

    /**
     * Get the stream to write warnings or errors to.
     */
    std::ostream* getErrorStream() const;

    /**
     * Get the serialized file tail that the user passed in.
     */
    std::string getSerializedFileTail() const;

    /**
     * Get the desired tail location.
     * @return if not set, return the maximum long.
     */
    uint64_t getTailLocation() const;

    /**
     * Get the memory allocator.
     */
    MemoryPool* getMemoryPool() const;

    /**
     * Get the reader metrics.
     */
    ReaderMetrics* getReaderMetrics() const;

    /**
     * Set the cache options.
     */
    const CacheOptions& getCacheOptions() const;
  };

  /**
   * Options for creating a RowReader.
   */
  class RowReaderOptions {
   private:
    std::unique_ptr<RowReaderOptionsPrivate> privateBits_;

   public:
    RowReaderOptions();
    RowReaderOptions(const RowReaderOptions&);
    RowReaderOptions(RowReaderOptions&);
    RowReaderOptions& operator=(const RowReaderOptions&);
    virtual ~RowReaderOptions();

    /**
     * For files that have structs as the top-level object, select the fields
     * to read. The first field is 0, the second 1, and so on. By default,
     * all columns are read. This option clears any previous setting of
     * the selected columns.
     * @param include a list of fields to read
     * @return this
     */
    RowReaderOptions& include(const std::list<uint64_t>& include);

    /**
     * For files that have structs as the top-level object, select the fields
     * to read by name. By default, all columns are read. This option clears
     * any previous setting of the selected columns.
     * @param include a list of fields to read
     * @return this
     */
    RowReaderOptions& include(const std::list<std::string>& include);

    /**
     * Selects which type ids to read. The root type is always 0 and the
     * rest of the types are labeled in a preorder traversal of the tree.
     * The parent types are automatically selected, but the children are not.
     *
     * This option clears any previous setting of the selected columns or
     * types.
     * @param types a list of the type ids to read
     * @return this
     */
    RowReaderOptions& includeTypes(const std::list<uint64_t>& types);

    /**
     * A map type of <typeId, ReadIntent>.
     */
    typedef std::map<uint64_t, ReadIntent> IdReadIntentMap;

    /**
     * Selects which type ids to read and specific ReadIntents for each
     * type id. The ancestor types are automatically selected, but the children
     * are not.
     *
     * This option clears any previous setting of the selected columns or
     * types.
     * @param idReadIntentMap a map of IdReadIntentMap.
     * @return this
     */
    RowReaderOptions& includeTypesWithIntents(const IdReadIntentMap& idReadIntentMap);

    /**
     * Set the section of the file to process.
     * @param offset the starting byte offset
     * @param length the number of bytes to read
     * @return this
     */
    RowReaderOptions& range(uint64_t offset, uint64_t length);

    /**
     * For Hive 0.11 (and 0.12) decimals, the precision was unlimited
     * and thus may overflow the 38 digits that is supported. If one
     * of the Hive 0.11 decimals is too large, the reader may either convert
     * the value to NULL or throw an exception. That choice is controlled
     * by this setting.
     *
     * Defaults to true.
     *
     * @param shouldThrow should the reader throw a ParseError?
     * @return returns *this
     */
    RowReaderOptions& throwOnHive11DecimalOverflow(bool shouldThrow);

    /**
     * For Hive 0.11 (and 0.12) written decimals, which have unlimited
     * scale and precision, the reader forces the scale to a consistent
     * number that is configured. This setting changes the scale that is
     * forced upon these old decimals. See also throwOnHive11DecimalOverflow.
     *
     * Defaults to 6.
     *
     * @param forcedScale the scale that will be forced on Hive 0.11 decimals
     * @return returns *this
     */
    RowReaderOptions& forcedScaleOnHive11Decimal(int32_t forcedScale);

    /**
     * Set enable encoding block mode.
     * By enable encoding block mode, Row Reader will not decode
     * dictionary encoded string vector, but instead return an index array with
     * reference to corresponding dictionary.
     */
    RowReaderOptions& setEnableLazyDecoding(bool enable);

    /**
     * Set search argument for predicate push down
     */
    RowReaderOptions& searchArgument(std::unique_ptr<SearchArgument> sargs);

    /**
     * Should enable encoding block mode
     */
    bool getEnableLazyDecoding() const;

    /**
     * Were the field ids set?
     */
    bool getIndexesSet() const;

    /**
     * Were the type ids set?
     */
    bool getTypeIdsSet() const;

    /**
     * Get the list of selected field or type ids to read.
     */
    const std::list<uint64_t>& getInclude() const;

    /**
     * Were the include names set?
     */
    bool getNamesSet() const;

    /**
     * Get the list of selected columns to read. All children of the selected
     * columns are also selected.
     */
    const std::list<std::string>& getIncludeNames() const;

    /**
     * Get the start of the range for the data being processed.
     * @return if not set, return 0
     */
    uint64_t getOffset() const;

    /**
     * Get the end of the range for the data being processed.
     * @return if not set, return the maximum long
     */
    uint64_t getLength() const;

    /**
     * Should the reader throw a ParseError when a Hive 0.11 decimal is
     * larger than the supported 38 digits of precision? Otherwise, the
     * data item is replaced by a NULL.
     */
    bool getThrowOnHive11DecimalOverflow() const;

    /**
     * What scale should all Hive 0.11 decimals be normalized to?
     */
    int32_t getForcedScaleOnHive11Decimal() const;

    /**
     * Get search argument for predicate push down
     */
    std::shared_ptr<SearchArgument> getSearchArgument() const;

    /**
     * Set desired timezone to return data of timestamp type
     */
    RowReaderOptions& setTimezoneName(const std::string& zoneName);

    /**
     * Get desired timezone to return data of timestamp type
     */
    const std::string& getTimezoneName() const;

    /**
     * Get the IdReadIntentMap map that was supplied by client.
     */
    const IdReadIntentMap getIdReadIntentMap() const;

    /**
     * Set whether use fixed width numeric vectorBatch or not, such as int32_t / int16_t / int8_t /
     * float vectorBatch.
     */
    RowReaderOptions& setUseTightNumericVector(bool useTightNumericVector);

    /**
     * Get whether or not to use fixed width numeric columnVectorBatch.
     * @return if not set, the default is false
     */
    bool getUseTightNumericVector() const;

    /**
     * Set read type for schema evolution
     */
    RowReaderOptions& setReadType(std::shared_ptr<Type> type);

    /**
     * Get read type for schema evolution
     */
    std::shared_ptr<Type>& getReadType() const;

    /**
     * Set whether reader throws or returns null when value overflows for schema evolution.
     */
    RowReaderOptions& throwOnSchemaEvolutionOverflow(bool shouldThrow);

    /**
     * Whether reader throws or returns null when value overflows for schema evolution.
     */
    bool getThrowOnSchemaEvolutionOverflow() const;
  };

  class RowReader;

  /**
   * The interface for reading ORC file meta-data and constructing RowReaders.
   * This is an an abstract class that will be subclassed as necessary.
   */
  class Reader {
   public:
    virtual ~Reader();

    /**
     * Get the format version of the file. Currently known values are:
     * 0.11 and 0.12
     * @return the FileVersion object
     */
    virtual FileVersion getFormatVersion() const = 0;

    /**
     * Get the number of rows in the file.
     * @return the number of rows
     */
    virtual uint64_t getNumberOfRows() const = 0;

    /**
     * Get the software instance and version that wrote this file.
     * @return a user-facing string that specifies the software version
     */
    virtual std::string getSoftwareVersion() const = 0;

    /**
     * Get the user metadata keys.
     * @return the set of user metadata keys
     */
    virtual std::list<std::string> getMetadataKeys() const = 0;

    /**
     * Get a user metadata value.
     * @param key a key given by the user
     * @return the bytes associated with the given key
     */
    virtual std::string getMetadataValue(const std::string& key) const = 0;

    /**
     * Did the user set the given metadata value.
     * @param key the key to check
     * @return true if the metadata value was set
     */
    virtual bool hasMetadataValue(const std::string& key) const = 0;

    /**
     * Get the compression kind.
     * @return the kind of compression in the file
     */
    virtual CompressionKind getCompression() const = 0;

    /**
     * Get the buffer size for the compression.
     * @return number of bytes to buffer for the compression codec.
     */
    virtual uint64_t getCompressionSize() const = 0;

    /**
     * Get ID of writer that generated the file.
     * @return UNKNOWN_WRITER if the writer ID is undefined
     */
    virtual WriterId getWriterId() const = 0;

    /**
     * Get the writer id value when getWriterId() returns an unknown writer.
     * @return the integer value of the writer ID.
     */
    virtual uint32_t getWriterIdValue() const = 0;

    /**
     * Get the version of the writer.
     * @return the version of the writer.
     */
    virtual WriterVersion getWriterVersion() const = 0;

    /**
     * Get the number of rows per an entry in the row index.
     * @return the number of rows per an entry in the row index or 0 if there
     * is no row index.
     */
    virtual uint64_t getRowIndexStride() const = 0;

    /**
     * Get the number of stripes in the file.
     * @return the number of stripes
     */
    virtual uint64_t getNumberOfStripes() const = 0;

    /**
     * Get the information about a stripe.
     * @param stripeIndex the index of the stripe (0 to N-1) to get information about
     * @return the information about that stripe
     */
    virtual std::unique_ptr<StripeInformation> getStripe(uint64_t stripeIndex) const = 0;

    /**
     * Get the number of stripe statistics in the file.
     * @return the number of stripe statistics
     */
    virtual uint64_t getNumberOfStripeStatistics() const = 0;

    /**
     * Get the statistics about a stripe.
     * @param stripeIndex the index of the stripe (0 to N-1) to get statistics about
     * @return the statistics about that stripe
     */
    virtual std::unique_ptr<StripeStatistics> getStripeStatistics(uint64_t stripeIndex) const = 0;

    /**
     * Get the length of the data stripes in the file.
     * @return the number of bytes in stripes
     */
    virtual uint64_t getContentLength() const = 0;

    /**
     * Get the length of the file stripe statistics.
     * @return the number of compressed bytes in the file stripe statistics
     */
    virtual uint64_t getStripeStatisticsLength() const = 0;

    /**
     * Get the length of the file footer.
     * @return the number of compressed bytes in the file footer
     */
    virtual uint64_t getFileFooterLength() const = 0;

    /**
     * Get the length of the file postscript.
     * @return the number of bytes in the file postscript
     */
    virtual uint64_t getFilePostscriptLength() const = 0;

    /**
     * Get the total length of the file.
     * @return the number of bytes in the file
     */
    virtual uint64_t getFileLength() const = 0;

    /**
     * Get the statistics about the columns in the file.
     * @return the information about the column
     */
    virtual std::unique_ptr<Statistics> getStatistics() const = 0;

    /**
     * Get the statistics about a single column in the file.
     * @param columnId id of the column
     * @return the information about the column
     */
    virtual std::unique_ptr<ColumnStatistics> getColumnStatistics(uint32_t columnId) const = 0;

    /**
     * Check if the file has correct column statistics.
     */
    virtual bool hasCorrectStatistics() const = 0;

    /**
     * Get metrics of the reader
     * @return the accumulated reader metrics to current state.
     */
    virtual const ReaderMetrics* getReaderMetrics() const = 0;

    /**
     * Get the serialized file tail.
     * Usefull if another reader of the same file wants to avoid re-reading
     * the file tail. See ReaderOptions.setSerializedFileTail().
     * @return a string of bytes with the file tail
     */
    virtual std::string getSerializedFileTail() const = 0;

    /**
     * Get the type of the rows in the file. The top level is typically a
     * struct.
     * @return the root type
     */
    virtual const Type& getType() const = 0;

    /**
     * Create a RowReader based on this reader with the default options.
     * @return a RowReader to read the rows
     */
    virtual std::unique_ptr<RowReader> createRowReader() const = 0;

    /**
     * Create a RowReader based on this reader.
     * @param options RowReader Options
     * @return a RowReader to read the rows
     */
    virtual std::unique_ptr<RowReader> createRowReader(const RowReaderOptions& options) const = 0;

    /**
     * Get the name of the input stream.
     */
    virtual const std::string& getStreamName() const = 0;

    /**
     * Estimate an upper bound on heap memory allocation by the Reader
     * based on the information in the file footer.
     * The bound is less tight if only few columns are read or compression is
     * used.
     */
    /**
     * @param stripeIx index of the stripe to be read (if not specified,
     *        all stripes are considered).
     * @return upper bound on memory use by all columns
     */
    virtual uint64_t getMemoryUse(int stripeIx = -1) = 0;

    /**
     * @param include Column Field Ids
     * @param stripeIx index of the stripe to be read (if not specified,
     *        all stripes are considered).
     * @return upper bound on memory use by selected columns
     */
    virtual uint64_t getMemoryUseByFieldId(const std::list<uint64_t>& include,
                                           int stripeIx = -1) = 0;

    /**
     * @param names Column Names
     * @param stripeIx index of the stripe to be read (if not specified,
     *        all stripes are considered).
     * @return upper bound on memory use by selected columns
     */
    virtual uint64_t getMemoryUseByName(const std::list<std::string>& names, int stripeIx = -1) = 0;

    /**
     * @param include Column Type Ids
     * @param stripeIx index of the stripe to be read (if not specified,
     *        all stripes are considered).
     * @return upper bound on memory use by selected columns
     */
    virtual uint64_t getMemoryUseByTypeId(const std::list<uint64_t>& include,
                                          int stripeIx = -1) = 0;

    /**
     * Get BloomFiters of all selected columns in the specified stripe
     * @param stripeIndex index of the stripe to be read for bloom filters.
     * @param included index of selected columns to return (if not specified,
     *        all columns that have bloom filters are considered).
     * @return map of bloom filters with the key standing for the index of column.
     */
    virtual std::map<uint32_t, BloomFilterIndex> getBloomFilters(
        uint32_t stripeIndex, const std::set<uint32_t>& included) const = 0;

    /**
     * Get row group index of all selected columns in the specified stripe
     * @param stripeIndex index of the stripe to be read for row group index.
     * @param included index of selected columns to return (if not specified,
     *        all columns will be returned).
     * @return map of row group index keyed by its column index.
     */
    virtual std::map<uint32_t, RowGroupIndex> getRowGroupIndex(
        uint32_t stripeIndex, const std::set<uint32_t>& included = {}) const = 0;

    /**
     * Trigger IO prefetch and cache the prefetched contents asynchronously.
     * It is thread safe. Users should make sure requested stripes and columns
     * are not overlapped, otherwise the overlapping part will be prefetched multiple time,
     * which doesn't affect correctness but waste IO and memory resources.
     * @param stripes the stripes to prefetch
     * @param includeTypes the types to prefetch
     */
    virtual void preBuffer(const std::vector<uint32_t>& stripes,
                           const std::list<uint64_t>& includeTypes) = 0;

    /**
     * Release cached entries whose right boundary is less than or equal to the given boundary.
     * @param boundary the boundary value to release cache entries
     */
    virtual void releaseBuffer(uint64_t boundary) = 0;
  };

  /**
   * The interface for reading rows in ORC files.
   * This is an an abstract class that will be subclassed as necessary.
   */
  class RowReader {
   public:
    virtual ~RowReader();
    /**
     * Get the selected type of the rows in the file. The file's row type
     * is projected down to just the selected columns. Thus, if the file's
     * type is struct<col0:int,col1:double,col2:string> and the selected
     * columns are "col0,col2" the selected type would be
     * struct<col0:int,col2:string>.
     * @return the root type
     */
    virtual const Type& getSelectedType() const = 0;

    /**
     * Get the selected columns of the file.
     */
    virtual const std::vector<bool> getSelectedColumns() const = 0;

    /**
     * Create a row batch for reading the selected columns of this file.
     * @param size the number of rows to read
     * @return a new ColumnVectorBatch to read into
     */
    virtual std::unique_ptr<ColumnVectorBatch> createRowBatch(uint64_t size) const = 0;

    /**
     * Read the next row batch from the current position.
     * Caller must look at numElements in the row batch to determine how
     * many rows were read.
     * @param data the row batch to read into.
     * @return true if a non-zero number of rows were read or false if the
     *   end of the file was reached.
     */
    virtual bool next(ColumnVectorBatch& data) = 0;

    /**
     * Get the row number of the first row in the previously read batch.
     * @return the row number of the previous batch.
     */
    virtual uint64_t getRowNumber() const = 0;

    /**
     * Seek to a given row.
     * @param rowNumber the next row the reader should return
     */
    virtual void seekToRow(uint64_t rowNumber) = 0;
  };
}  // namespace orc

#endif
