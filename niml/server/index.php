<? $_ARGS["name"] = "visitor";
if (isset($_SESSION["user"])) {
    $_ARGS["name"] = $_SESSION["user"];
} ?>

<? echo generate_header(""); ?>

{var: "zzzzzz"}
{cb: [[]
    [print "In callback"]
    [replace _output [tag _output var]]
    [document]
]}

[tag content

Hello <? echo $_ARGS["name"]; ?>

[rem message_admin "http://example.com/index.php"]

Welcome to Home Center a place for homes.

"Recent Posts:"

<?
$dir = new DirectoryIterator($_SERVER_ROOT . "/posts/");
foreach ($dir as $fileinfo) {
    if ($fileinfo->isDot()) {
        continue;
    }
    $post_name = $fileinfo->getFilename();
    ?>
[button "<?= $post_name; ?>" [[]
    [navigate "/posts/<?= $post_name; ?>"]
]]
<?
}
?>

[rem tag _output "whats up"]

[rem button "hello world" cb]

]
