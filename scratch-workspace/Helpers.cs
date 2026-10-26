namespace Scratch;

public static class Helpers
{
    public static string Greeting(string name) => $"Hello, {name}!";

    public static int Clamp(int value, int min, int max) => Math.Min(Math.Max(value, min), max);

    public static string Slugify(string text) =>
        string.Join("-", text.ToLowerInvariant().Split(' ', StringSplitOptions.RemoveEmptyEntries));

    public static IEnumerable<T[]> Chunk<T>(IEnumerable<T> source, int size) => source.Chunk(size);

    public static async Task<T> RetryAsync<T>(Func<Task<T>> action, int attempts = 3)
    {
        for (var attempt = 1; ; attempt++)
        {
            try
            {
                return await action();
            }
            catch (Exception) when (attempt < attempts)
            {
                await Task.Delay(100 * attempt);
            }
        }
    }
}
