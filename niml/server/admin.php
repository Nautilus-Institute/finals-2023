<? if (!isset($_SESSION["user"]) || $_SESSION["user"] != "admin") { ?>
[navigate "/login.php"]
<? } else { ?>
<? echo generate_header("Admin Interface"); ?>

<?= server_info() ?>

<? } ?>