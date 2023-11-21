<? if (isset($_ARGS["title"]) && isset($_ARGS["body"])) { 
    $user = "anon";
    if (isset($_SESSION["user"])) {
        $user = $_SESSION["user"];
    }
    $title = prepare_title($_ARGS["title"]);
    $body = prepare_body($_ARGS["body"]);
    $title = 

    $post = <<<EOF
<? NIMS_sandbox_start(); ?>

<? echo generate_header("$title"); ?>

Author:
$user

Body:
$body

EOF;
    $post_id = $user . "_" . time() . ".niml";
    $post_path = $_SERVER_ROOT . "posts/" . $post_id;
    file_put_contents($post_path, $post);
    ?> [navigate "/posts/<?= $post_id; ?>"] <?
    return;
}?>

<? echo generate_header("Submit Post"); ?>
[tag content

Post Title
[input "title" {form: _submit}]

Post Content
[input "body" {form: _submit}]

[checkbox "private" "Make post private"]

[button "Post" [[form]
    [navigate "submit.php" form] 
]{
    form: _submit
}]

]