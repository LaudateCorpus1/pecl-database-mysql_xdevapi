--TEST--
mysqlx collection row locking parallel exclusive write before shared read
--SKIPIF--
--INI--
error_reporting=0
--FILE--
<?php
	require_once(__DIR__."/../connect.inc");
	require_once(__DIR__."/mysqlx_row_locking.inc");

	assert_mysql_xdevapi_loaded();

	$session = mysql_xdevapi\getSession($connection_uri);
	$coll = createTestCollection($session);

	$worker_process = run_worker(__FILE__);
	if (is_resource($worker_process))
	{
		recv_worker_started();

		$session->startTransaction();
		check_find_lock_all($coll, ['1', '2'], [1, 2], $Lock_exclusive);
		modify_row($coll, '1', 11);
		modify_row($coll, '2', 22);
		check_find_lock_all($coll, ['1', '2'], [11, 22], $Lock_exclusive);

		$expected_result = "1 2";
		recv_msg_from_worker($expected_result);

		$session->commit(); // worker should unblock now

		send_let_worker_commit();
		recv_worker_committed();

		$expected_result = "11 22";
		recv_msg_from_worker($expected_result);

		check_find_lock_one($coll, '1', 11, $Lock_exclusive);
		check_find_lock_one($coll, '2', 22, $Lock_exclusive);
	}

	verify_expectations();
	print "done!\n";
?>
--CLEAN--
<?php
	require(__DIR__."/../connect.inc");
	clean_test_db();
?>
--EXPECTF--
worker cmd-line:%s
worker started
1 2
let worker commit
worker committed
11 22
done!%A