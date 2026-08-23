$p = RandomNumber(1);

SELECT Rowid::newColumnKey() != Rowid::newColumnKey() AS column_key_unique;
SELECT Rowid::newRowKey() != Rowid::newRowKey() AS row_key_unique;

SELECT Rowid::newColumnKey(1) != Rowid::newColumnKey(2) AS column_key_dep_unique;
SELECT Rowid::newRowKey(1) != Rowid::newRowKey(2) AS row_key_dep_unique;
SELECT Rowid::newColumnKey(1, 2, 3) != Rowid::newColumnKey(1, 2, 4) AS column_key_three_dep_unique;
SELECT Rowid::newRowKey(1, 2, 3) != Rowid::newRowKey(1, 2, 4) AS row_key_three_dep_unique;

$group = Rowid::newRowGroup($p, 3ul);
SELECT ListLength($group) = 3ul AS row_group_count;
SELECT Unwrap($group[0]) != Unwrap($group[1]) AND Unwrap($group[1]) != Unwrap($group[2]) AS row_group_distinct;

$groupFromRowid = Rowid::newRowGroup(Unwrap($group[0]), 2ul);
SELECT ListLength($groupFromRowid) = 2ul AS row_group_rowid_prefix_count;
SELECT Unwrap($groupFromRowid[0]) != Unwrap($groupFromRowid[1]) AS row_group_rowid_prefix_distinct;

$groupDep = Rowid::newRowGroup($p, 2ul, 1);
$groupDep2 = Rowid::newRowGroup($p, 2ul, 2);
SELECT Unwrap($groupDep[0]) != Unwrap($groupDep2[0]) AS row_group_dep_unique;

SELECT Length(CAST(Rowid::newColumnKey() AS String)) = 19ul AS column_key_base64_length;
SELECT Length(CAST(Rowid::newRowKey() AS String)) = 19ul AS row_key_base64_length;

SELECT $p != 0ul OR $p == 0ul AS prefix_is_uint64;
