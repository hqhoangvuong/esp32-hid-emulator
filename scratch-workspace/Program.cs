using Scratch;

Console.WriteLine(Helpers.Greeting("world"));
Console.WriteLine(Helpers.Slugify("Developer Simulator Scratch"));
Console.WriteLine(Helpers.Clamp(42, 0, 10));

foreach (var chunk in Helpers.Chunk(Enumerable.Range(1, 7), 3))
{
    Console.WriteLine(string.Join(",", chunk));
}

var answer = await Helpers.RetryAsync(() => Task.FromResult(42));
Console.WriteLine(answer);
